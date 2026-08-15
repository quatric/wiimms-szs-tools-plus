// External pass-through for container formats added by the Nintendo fork.
// When XX/EXTRACT/XCOMMON encounter a file that is neither a native archive
// nor a raw Nintendo codec stream (see lib-nintendo), it is handed to one of
// the external unpackers (wit, ndstool, ctrtool, sharpii).  The external tool
// unpacks the container into a staging directory; the caller then recurses
// into that directory like any other extracted archive.
#include "lib-passthru.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include "dclib-basics.h"
#include "dclib-color.h"
#include "dclib-debug.h"
#include "dclib-file.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-bms.h"
#include "lib-aes.h"

// option state, bound in tab-wszst.inc / CheckOptions() of wszst.c
bool opt_no_passthrough = false;	// --no-passthrough: disable pass-through
ccp	opt_with_wit	= 0;		// --with-wit=path|name
ccp	opt_with_ndstool	= 0;		// --with-ndstool=path|name
ccp	opt_with_ctrtool	= 0;		// --with-ctrtool=path|name
ccp	opt_with_sharpii	= 0;		// --with-sharpii=path|name
ccp	opt_with_hactool	= 0;		// --with-hactool=path|name
ccp	opt_with_bms		= 0;		// --with-bms=path|--bms=path

// Curried static result buffer, only valid until the next call.  Reasonable
// here since these helpers are used from single-threaded option parsing.
static char prog_buf[PATH_MAX];

static enumError passthru_claim ( bool strong_only, ccp src, ccp basedir,
	char *staged_dir, uint staged_dir_size );

static enumError passthru_archive ( ccp src, ccp basedir, ccp stage,
	char *staged_dir, uint staged_dir_size,
	bool is_ds, bool is_ctr, bool is_wad, bool is_disc, bool is_switch );

static enumError passthru_archive_or_bms ( ccp src, ccp basedir, ccp stage,
	char *staged_dir, uint staged_dir_size,
	bool is_ds, bool is_ctr, bool is_wad, bool is_disc, bool is_switch );

// Turn a possibly relative tool name/path into an absolute one by scanning
// PATH.  Returns the resolved name or NULL when not found.
static const char * find_program ( ccp name )
{
    if ( !name )
	return 0;

    if ( strchr(name,'/') )
	return (ccp)strcpy(prog_buf,name);

    const char *dirs = getenv("PATH");
    if ( !dirs )
	return 0;

    while ( dirs && *dirs )
    {
	ccp end = strchr(dirs,':');
	const uint len = end ? (uint)(end-dirs) : (uint)strlen(dirs);
	if (len)
	{
	    snprintf(prog_buf,sizeof(prog_buf),"%.*s/%s",(int)len,dirs,name);
	    if ( !access( prog_buf, X_OK ) )
		return prog_buf;
	}
	dirs = end ? end+1 : 0;
    }
    return 0;
}

// Look up the tool the user requested.  WITH_VAL is the --with-<tool> value;
// DEFAULT is the bare name used for a plain PATH search.
static ccp resolve_tool ( ccp with_val, ccp deflt )
{
    if ( with_val && *with_val )
	return find_program(with_val);
    return find_program(deflt);
}

// Spawn a program with ARGV (NULL-terminated).  ARGV[0] is used as path.
// STDOUT/STDERR are inherited so the user sees the tool's own messages.
// Returns the exit code or 127 on exec failure (like a shell).
static int run_program ( char * const argv[] )
{
    const pid_t pid = fork();
    if ( pid < 0 )
	return -errno;
    if ( pid == 0 )
    {
	execv(argv[0],argv);
	_Exit(127); // execv failed
    }

    int status = 0;
    while ( waitpid(pid,&status,0) < 0 && errno == EINTR )
	;
    if ( WIFEXITED(status) )
	return WEXITSTATUS(status);
    return -1;
}

// Read the first N bytes of a file. Zeroes buffer first for partial reads.
static bool read_head ( ccp src, u8 *buf, uint n )
{
    FILE *f = fopen(src,"rb");
    if ( !f ) return false;
    memset(buf,0,n);
    const size_t got = fread(buf,1,n,f);
    const bool ok = (got > 0 || feof(f)) && !ferror(f);
    fclose(f);
    return ok;
}

static bool is_ext ( ccp src, ccp ext )
{
    const uint n = strlen(src);
    const uint m = strlen(ext);
    if ( n < m ) return false;
    return strcasecmp(src+n-m,ext)==0;
}

// True for the file extensions used by disc image tools (wit).
static bool is_disc_ext ( ccp src )
{
    return is_ext(src,".wbfs") || is_ext(src,".wdf")  || is_ext(src,".ciso")
	|| is_ext(src,".iso") || is_ext(src,".gcm")    || is_ext(src,".gca")
	|| is_ext(src,".wia") || is_ext(src,".raw")    || is_ext(src,".img");
}

// Build the staging directory for SRC: BASEDIR (may be NULL or "" for none)
// plus the file name without extension plus ".d".  Returns false when SRC has
// no basename at all.
//
// Without an explicit BASEDIR (no --dest / no --dest-base), every other
// extractor in this codebase stages beside the source via the "\1P/\1N.d"
// SubstDest pattern -- i.e. the source's own directory, not the process's
// current working directory. stage_dir_of() used to fall back to a bare
// "<stem>.d" in that case, so `wszst XX /some/other/dir/foo.nds` from an
// unrelated cwd staged into "./foo.d" instead of "/some/other/dir/foo.d"
// (found by testing a real .nds from a scratch dir while cwd was elsewhere).
static bool stage_dir_of ( ccp src, ccp basedir, char *buf, uint bufsize )
{
    ccp basename = strrchr(src,'/');
    basename = basename ? basename+1 : src;

    char stem[PATH_MAX];
    strncpy(stem,basename,sizeof(stem)-1);
    stem[sizeof(stem)-1] = 0;
    char *dot = strrchr(stem,'.');
    if ( dot && dot > stem )
	*dot = 0;

    if ( basedir && *basedir )
	snprintf(buf,bufsize,"%s%s.d",basedir,stem);
    else if ( basename != src )
	snprintf(buf,bufsize,"%.*s%s.d",(int)(basename-src),src,stem);
    else
	snprintf(buf,bufsize,"%s.d",stem);
    return *buf != 0;
}

// Report a recognized-but-unsupported pass-through container: the external
// tool for it is not available and the caller should skip the file.
static enumError make_stage_dir ( ccp stage, bool tool_missing )
{
    if (tool_missing)
	return ERROR0(ERR_WARNING,"Pass-through tool not found; install it "
	    "or select it with --with-<tool>=<path>: %s",stage);

    return ERR_OK;
}

// Run the user's --bms=<script> against SRC, staging into STAGE. Returns
// ERR_NOTHING_TO_DO if no BMS script is configured (or during the strong,
// header-only pass, which BMS never participates in) so callers can chain
// it after any other claim attempt.
static enumError run_bms_fallback
(
    bool	strong_only,
    ccp		src,
    ccp		stage,
    char	* staged_dir,
    uint	staged_dir_size
)
{
    if ( strong_only || !opt_with_bms || !*opt_with_bms )
	return ERR_NOTHING_TO_DO;

    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%s%sEXTRACT BMS: %s -> %s (%s)\n",
	    testmode ? "WOULD " : "", verbose>0 ? "\n" : "", src, stage, opt_with_bms );

    if ( testmode )
    {
	snprintf(staged_dir,staged_dir_size,"%s",stage);
	return ERR_OK;
    }

    if ( CreatePath(stage,false) )
	return ERROR0(ERR_CANT_CREATE_DIR,"Cannot create dest dir: %s",stage);

    const enumError bms_err = RunBmsScript(opt_with_bms,src,stage);
    if ( bms_err == ERR_OK )
    {
	snprintf(staged_dir,staged_dir_size,"%s",stage);
	return ERR_OK;
    }
    return bms_err;
}

// Call passthru_archive(); when the external tool for this container type
// is missing, fall through to the user's --bms=<script> instead of just
// failing -- this is the "game image extractor" (wit/ndstool/ctrtool/
// hactool/sharpii) path, and a BMS script is a legitimate substitute for
// any of them (e.g. a custom or newer container hactool doesn't know yet).
// Without this, a claim like the Switch NCA/NSP/XCI one below returned
// immediately on a missing hactool and BMS was never reached, even though
// the final fallback later in passthru_claim() would have tried it for an
// otherwise-unclaimed file.
//
// Deliberately ignores STRONG_ONLY here (unlike the final catch-all
// fallback in passthru_claim(), which still respects it): a container
// already claimed by header signature or extension has no native decoder
// to conflict with once its own tool turns out to be missing, so there is
// nothing left for a strong-pass-only restriction to protect -- most real
// containers (NSP/XCI/NCA, NCCH/NCSD, WBFS/ISO/etc.) are claimed in the
// strong pass, which is exactly where this fallback needs to fire.
static enumError passthru_archive_or_bms
(
    ccp		src,
    ccp		basedir,
    ccp		stage,
    char	* staged_dir,
    uint	staged_dir_size,
    bool	is_ds,
    bool	is_ctr,
    bool	is_wad,
    bool	is_disc,
    bool	is_switch
)
{
    const enumError err = passthru_archive(src,basedir,stage,
	staged_dir,staged_dir_size, is_ds,is_ctr,is_wad,is_disc,is_switch);
    if ( err == ERR_WARNING )
    {
	const enumError bms_err = run_bms_fallback(false,src,stage,staged_dir,staged_dir_size);
	if ( bms_err != ERR_NOTHING_TO_DO )
	    return bms_err;
    }
    return err;
}

// If PATH decodes as valid BLZ, overwrite it with the decompressed bytes.
// Silent no-op (not an error) if it doesn't -- most arm7.bin/overlay files
// in particular are often already plain, and ndstool gives no signal
// either way.
static void try_decompress_blz_inplace ( ccp path )
{
    u8 *raw = 0; size_t raw_size = 0;
    if ( LoadFileAlloc(path,0,0,&raw,&raw_size,0,0,0,false) || !raw || raw_size > UINT_MAX )
    {
	if (raw) FREE(raw);
	return;
    }

    u8 *dest = 0; uint dest_size = 0;
    const enumError err = DecodeBLZ(&dest,&dest_size,raw,(uint)raw_size);
    FREE(raw);
    if (err || !dest)
	return;

    File_t F;
    if ( !CreateFileOpt(&F,true,path,false,path) && F.f )
	fwrite(dest,1,dest_size,F.f);
    ResetFile(&F,false);
    FREE(dest);
}

// Sweep every regular file directly inside DIR through
// try_decompress_blz_inplace() -- used for ndstool's overlay/ directory,
// where overlay count and filenames vary per game.
static void blz_decompress_dir ( ccp dir )
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ( (e = readdir(d)) != 0 )
    {
	if ( e->d_name[0] == '.' )
	    continue;
	char path[PATH_MAX];
	snprintf(path,sizeof(path),"%s/%s",dir,e->d_name);
	struct stat st;
	if ( !stat(path,&st) && S_ISREG(st.st_mode) )
	    try_decompress_blz_inplace(path);
    }
    closedir(d);
}

// hactool's own --titlekey option expects the RAW titlekey exactly as it is
// stored in the ticket (still titlekek-encrypted) -- it decrypts it itself
// internally using the NCA's own key generation. This function must hand
// that raw key straight through, never pre-decrypt it: doing so silently
// double-decrypts (hactool decrypts an already-decrypted key with the
// titlekek a second time), producing garbage content-section keys. hactool
// reports no error for this -- it prints "Error: section N is corrupted!"
// per hash-mismatched section and writes zero bytes for it, with a normal
// (0) exit code, so the pass-through looks like it "worked" while silently
// producing an empty tree. Confirmed live on Super Mario Odyssey's 5.5 GB
// Program NCA: passing the pre-decrypted key (as this function used to)
// corrupted both sections; passing the raw ticket key unmodified extracted
// the real romfs tree (thousands of real .szs files) cleanly.
static void find_nca_titlekey ( ccp nca_path, char *out_titlekey, size_t out_size )
{
    out_titlekey[0] = '\0';
    u8 rights_id[16] = {0};
    bool has_rights = false;

    FILE *f = fopen(nca_path, "rb");
    if (f)
    {
	u8 hdr[0x400];
	size_t got = fread(hdr, 1, sizeof(hdr), f);
	fclose(f);
	if (got >= 0x220)
	{
	    memcpy(rights_id, hdr + 0x204, 16);
	    for (int i = 0; i < 16; i++)
	    {
		if (rights_id[i] != 0) { has_rights = true; break; }
	    }
	}
    }

    char rights_hex[33] = "";
    if (has_rights)
    {
	for (int i = 0; i < 16; i++) snprintf(rights_hex + i*2, 3, "%02x", rights_id[i]);
    }

    // 1. Check sibling directory for .tik files
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", nca_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else snprintf(dir, sizeof(dir), ".");

    DIR *d = opendir(dir);
    if (d)
    {
        struct dirent *de;
        u8 fallback_tkey[16] = {0};
        bool has_fallback = false;

        while ((de = readdir(d)))
        {
            if (strstr(de->d_name, ".tik"))
            {
                char tik_path[PATH_MAX];
                snprintf(tik_path, sizeof(tik_path), "%s/%s", dir, de->d_name);
                FILE *tf = fopen(tik_path, "rb");
                if (tf)
                {
                    u8 tdata[0x300];
                    size_t tgot = fread(tdata, 1, sizeof(tdata), tf);
                    fclose(tf);
                    if (tgot >= 0x190)
                    {
                        bool nonzero = false;
                        for (int i = 0; i < 16; i++) {
                            if (tdata[0x180 + i] != 0) { nonzero = true; break; }
                        }
                        if (nonzero)
                        {
                            if (has_rights && tgot >= 0x2B0 && !memcmp(tdata + 0x2A0, rights_id, 16))
                            {
                                for (int i = 0; i < 16; i++)
                                    snprintf(out_titlekey + i*2, out_size - i*2, "%02x", tdata[0x180 + i]);
                                closedir(d);
                                return;
                            }
                            if (!has_fallback)
                            {
                                memcpy(fallback_tkey, tdata + 0x180, 16);
                                has_fallback = true;
                            }
                        }
                    }
                }
            }
        }
        closedir(d);

        if (has_fallback)
        {
            for (int i = 0; i < 16; i++)
                snprintf(out_titlekey + i*2, out_size - i*2, "%02x", fallback_tkey[i]);
            return;
        }
    }

    // 2. Check ~/.switch/title.keys
    const char *home = getenv("HOME");
    if (home)
    {
        char tkeys_path[PATH_MAX];
        snprintf(tkeys_path, sizeof(tkeys_path), "%s/.switch/title.keys", home);
        FILE *tkf = fopen(tkeys_path, "r");
        if (tkf)
        {
            char line[256];
            while (fgets(line, sizeof(line), tkf))
            {
                char *eq = strchr(line, '=');
                if (eq)
                {
                    *eq = '\0';
                    char *k = line;
                    while (*k == ' ' || *k == '\t') k++;
                    char *kend = eq - 1;
                    while (kend > k && (*kend == ' ' || *kend == '\t' || *kend == '\r' || *kend == '\n')) *kend-- = '\0';

                    char *v = eq + 1;
                    while (*v == ' ' || *v == '\t') v++;
                    char *vend = v + strlen(v) - 1;
                    while (vend > v && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n')) *vend-- = '\0';

                    if (has_rights && !strcasecmp(k, rights_hex) && strlen(v) >= 32)
                    {
                        // title.keys stores the same raw ticket-encrypted
                        // key hactool's --titlekey expects -- pass it
                        // through as-is, do not decrypt it here.
                        snprintf(out_titlekey, out_size, "%.32s", v);
                        fclose(tkf);
                        return;
                    }
                }
            }
            fclose(tkf);
        }
    }
}

// hactool's romfs extractor does not recursively create parent directory
// paths before attempting fopen(..., "wb"), causing "Failed to open ...!"
// write failures on nested files. Query the romfs directory listing first
// and pre-create all destination directories so hactool's writes succeed.
static void precreate_romfs_dirs
(
    ccp tool,
    ccp prod_keys,
    ccp titlekey,
    ccp src,
    ccp romfs_dir
)
{
    char cmd[PATH_MAX * 3];
    char k_opt[PATH_MAX + 16] = "";
    char t_opt[128] = "";
    if (prod_keys && *prod_keys)
        snprintf(k_opt, sizeof(k_opt), "-k \"%s\"", prod_keys);
    if (titlekey && *titlekey)
        snprintf(t_opt, sizeof(t_opt), "--titlekey=%s", titlekey);

    snprintf(cmd, sizeof(cmd), "\"%s\" %s %s --listromfs \"%s\" 2>/dev/null",
        tool, k_opt, t_opt, src);

    FILE *p = popen(cmd, "r");
    if (!p) return;

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), p))
    {
        char *nl = strchr(line, '\r'); if (nl) *nl = 0;
        nl = strchr(line, '\n'); if (nl) *nl = 0;

        ccp rel = 0;
        if (!memcmp(line, "romfs:/", 7))
            rel = line + 7;
        else if (!memcmp(line, "romfs:\\", 7))
            rel = line + 7;
        else if (strstr(line, "romfs:/"))
            rel = strstr(line, "romfs:/") + 7;

        if (rel && *rel)
        {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", romfs_dir, rel);
            CreatePath(path, false);
        }
    }
    pclose(p);
}

// Run the external unpacker for STAGE.  Exactly one of the DS/CTR/WAD flags
// is set.  SRC and BASEDIR are only used for messages; STAGE was produced by
// stage_dir_of() already and is filled into STAGED_DIR on success.
static enumError passthru_archive
(
    ccp		src,
    ccp		basedir,
    ccp		stage,
    char	* staged_dir,
    uint	staged_dir_size,
    bool	is_ds,		// true: ndstool
    bool	is_ctr,		// true: ctrtool
    bool	is_wad,		// true: sharpii
    bool	is_disc,	// true: wit
    bool	is_switch	// true: hactool
)
{
    ccp tool = is_disc   ? resolve_tool(opt_with_wit,"wit")
	     : is_ds     ? resolve_tool(opt_with_ndstool,"ndstool")
	     : is_ctr    ? resolve_tool(opt_with_ctrtool,"ctrtool")
	     : is_switch ? resolve_tool(opt_with_hactool,"hactool")
	     :	           resolve_tool(opt_with_sharpii,"sharpii");
    if ( !tool || !*tool )
    {
	*staged_dir = 0;
	return make_stage_dir(stage,true);
    }

    const ccp toolname = is_disc ? "wit" : is_ds ? "ndstool"
			: is_ctr ? "ctrtool" : is_switch ? "hactool" : "sharpii";
    if ( verbose >= 0 || testmode )
	fprintf(stdlog,"%s%sEXTRACT passthrough: %s -> %s (%s)\n",
	    testmode ? "WOULD " : "", verbose>0 ? "\n" : "", src, stage, toolname );

    if ( testmode )
    {
	snprintf(staged_dir,staged_dir_size,"%s",stage);
	return ERR_OK;
    }

    // the external tools require an existing destination directory ("-D" of
    // wit would create it too, but ndstool/sharpii/ctrtool need it upfront)
    if ( CreatePath(stage,false) )
	return ERROR0(ERR_CANT_CREATE_DIR,"Cannot create dest dir: %s",stage);

    if ( is_disc )
    {
	// -D: create the destination path automatically. -f: force, so a
	// stage dir left over from an earlier (partial/failed) run doesn't
	// make wit silently skip instead of extracting. -vv: wit's own
	// second verbosity level turns on its progress counter, useful for
	// diagnosing a stall/failure on a multi-GB disc image instead of
	// getting nothing but our own before/after log lines.
	char *argv[] = {
	    (char*)tool,
	    "EXTRACT",
	    "-D", stage,
	    "-f", "-vv",
	    (char*)src,
	    0
	};
	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'wit EXTRACT' failed for %s (exit %d)",src,rc);
    }
    else if ( is_ds )
    {
	// ndstool stages arm9.bin/arm7.bin/overlay files exactly as they sit
	// in the ROM -- BLZ-compressed if the game shipped them that way,
	// which is common. Decompressed in place below once ndstool runs;
	// try_decompress_blz_inplace() itself is the gate against corrupting
	// an already-plain executable: DecodeBLZ() only overwrites the file
	// if its footer is structurally consistent AND the LZSS walk fully
	// consumes the compressed span and lands exactly on the expected
	// output size -- anything else is left untouched, not guessed at.
	// ndstool has no "write rom info" flag; -y is the *overlay files*
	// output directory, not an XML sidecar. An earlier version of this
	// code passed "-y rominfo.xml" expecting a file and got an empty
	// directory named that instead (verified against the real ndstool
	// 2.3.1 CLI: "-y directory" is documented, there is no "-y file").
	char data_dir[PATH_MAX], overlay_dir[PATH_MAX];
	snprintf(data_dir,sizeof(data_dir),"%s/data",stage);
	snprintf(overlay_dir,sizeof(overlay_dir),"%s/overlay",stage);
	(void)CreatePath(data_dir,false);
	(void)CreatePath(overlay_dir,false);

	char arm9[PATH_MAX], arm7[PATH_MAX];
	snprintf(arm9,sizeof(arm9),"%s/arm9.bin",stage);
	snprintf(arm7,sizeof(arm7),"%s/arm7.bin",stage);

	char *argv[] = {
	    (char*)tool,
	    "-x", (char*)src,
	    "-9", arm9,
	    "-7", arm7,
	    "-d", data_dir,
	    "-y", overlay_dir,
	    0
	};
	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'ndstool -x' failed for %s (exit %d)",src,rc);

	try_decompress_blz_inplace(arm9);
	try_decompress_blz_inplace(arm7);
	blz_decompress_dir(overlay_dir);
    }
    else if ( is_ctr )
    {
	// ctrtool decrypts with its built-in retail common keys by default.
	// Point --exefsdir and --romfsdir at STAGE to produce recursible trees.
	char exefs_path[PATH_MAX], romfs_path[PATH_MAX];
	snprintf(exefs_path,sizeof(exefs_path),"%s/exefs",stage);
	snprintf(romfs_path,sizeof(romfs_path),"%s/romfs",stage);
	(void)CreatePath(exefs_path,false);
	(void)CreatePath(romfs_path,false);

	char exefsdir_arg[PATH_MAX], romfsdir_arg[PATH_MAX];
	snprintf(exefsdir_arg,sizeof(exefsdir_arg),"--exefsdir=%s",exefs_path);
	snprintf(romfsdir_arg,sizeof(romfsdir_arg),"--romfsdir=%s",romfs_path);

	char *argv[16];
	int argc = 0;
	argv[argc++] = (char*)tool;
	argv[argc++] = exefsdir_arg;
	argv[argc++] = romfsdir_arg;
	argv[argc++] = "--decompresscode";
	argv[argc++] = (char*)src;
	argv[argc] = 0;

	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'ctrtool' failed for %s (exit %d)",src,rc);
    }
    else if ( is_wad )
    {
	char *argv[] = {
	    (char*)tool,
	    "WAD", "-u", (char*)src, stage,
	    0
	};
	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'sharpii WAD -u' failed for %s (exit %d)",src,rc);
    }
    else if ( is_switch )
    {
	const char *home = getenv("HOME");
	char prod_keys[PATH_MAX] = "";
	if (home)
	{
	    snprintf(prod_keys, sizeof(prod_keys), "%s/.switch/prod.keys", home);
	    if (access(prod_keys, R_OK)) prod_keys[0] = '\0';
	}

	char titlekey_opt[128] = "";
	char romfs_dir[PATH_MAX], exefs_dir[PATH_MAX], sec0_dir[PATH_MAX];
	char pfs0_dir[PATH_MAX], xci_dir[PATH_MAX];
	char *argv[16];
	int argc = 0;
	argv[argc++] = (char*)tool;

	if (*prod_keys)
	{
	    argv[argc++] = "-k";
	    argv[argc++] = prod_keys;
	}

	if ( is_ext(src, ".nca") || is_ext(src, ".cnmt.nca") )
	{
	    argv[argc++] = "-x";
	    char romfs_path[PATH_MAX], exefs_path[PATH_MAX], sec0_path[PATH_MAX];
	    snprintf(romfs_path, sizeof(romfs_path), "%s/romfs", stage);
	    snprintf(exefs_path, sizeof(exefs_path), "%s/exefs", stage);
	    snprintf(sec0_path, sizeof(sec0_path), "%s/section0", stage);
	    (void)CreatePath(romfs_path, false);
	    (void)CreatePath(exefs_path, false);
	    (void)CreatePath(sec0_path, false);

	    snprintf(romfs_dir, sizeof(romfs_dir), "--romfsdir=%s", romfs_path);
	    snprintf(exefs_dir, sizeof(exefs_dir), "--exefsdir=%s", exefs_path);
	    snprintf(sec0_dir, sizeof(sec0_dir), "--section0dir=%s", sec0_path);
	    argv[argc++] = romfs_dir;
	    argv[argc++] = exefs_dir;
	    argv[argc++] = sec0_dir;

	    char tkey[64];
	    find_nca_titlekey(src, tkey, sizeof(tkey));
	    if (*tkey)
	    {
		snprintf(titlekey_opt, sizeof(titlekey_opt), "--titlekey=%s", tkey);
		argv[argc++] = titlekey_opt;
	    }
	    precreate_romfs_dirs(tool, prod_keys, tkey, src, romfs_path);
	}
	else if ( is_ext(src, ".xci") )
	{
	    argv[argc++] = "-x";
	    snprintf(xci_dir, sizeof(xci_dir), "--outdir=%s", stage);
	    argv[argc++] = xci_dir;
	}
	else
	{
	    argv[argc++] = "-x";
	    argv[argc++] = "-t";
	    argv[argc++] = "pfs0";
	    snprintf(pfs0_dir, sizeof(pfs0_dir), "--pfs0dir=%s", stage);
	    argv[argc++] = pfs0_dir;
	}

	argv[argc++] = (char*)src;
	argv[argc] = 0;

	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'hactool' failed for %s (exit %d)",src,rc);
    }

    snprintf(staged_dir,staged_dir_size,"%s",stage);
    return ERR_OK;
}

// Try to pass SRC through to an external unpacker.  Fills STAGED_DIR with the
// directory that received the payload (relative to BASEDIR) on success.
enumError PassthruExtract ( ccp src, ccp basedir, char *staged_dir, uint staged_dir_size )
{
    return passthru_claim(false,src,basedir,staged_dir,staged_dir_size);
}

// Strong-signature variant: only files that are unambiguously external
// containers by their HEADER (disc image magics, the DS "NINTENDO" tag) are
// claimed.  Called BEFORE the native probes in cmd_extract() so that huge
// disc images never trip the --max-file-size limited LoadFileAlloc() of the
// archive extractors.  Extension-only claims (.nds/.cia/.3ds/.cci/.cxi/
// .wad/.app) stay in the weak path so native decoders get first refusal.
enumError PassthruExtractStrong ( ccp src, ccp basedir, char *staged_dir, uint staged_dir_size )
{
    return passthru_claim(true,src,basedir,staged_dir,staged_dir_size);
}

static enumError passthru_claim
(
    bool	strong_only,	// true: header-claimed containers only
    ccp		src,
    ccp		basedir,
    char	* staged_dir,
    uint	staged_dir_size
)
{
    // containers are dispatched by their header signature; an extension
    // match is only tried when no signature is present.  0x420 covers the
    // XML-disc probe at 0x418 (0x400 prefix skip + 0x18 disc header).
    u8 head[0x420];
    if ( !read_head(src,head,sizeof(head)) )
	return ERR_NOTHING_TO_DO;

    char stage[PATH_MAX];
    if ( !stage_dir_of(src,basedir,stage,sizeof(stage)) )
	return ERR_NOTHING_TO_DO;

    // ----- claimed by header signature alone -----

    // Wii / GC disc images  (WBFS, WDF, CISO, raw ISO)
    //
    // The container headers are magic-keyed:
    //   WBFS  files start with the ASCII tag "WBFS".
    //   WDF   files start with the ASCII tag "WDF\0".
    //   CISO  files start with the ASCII tag "CISO".
    //   Raw   Wii/GC ISO images have no signature at offset 0; the disc
    //         header at offset 0x18 carries the wii_magic 0x5d1c9ea3 (GC
    //         uses 0xc2339f3d at 0x1c) right after the 0x18 byte title
    //         / game code area.  XML-disc images skip the 0x400 byte
    //         prefix, so probe 0x400 instead of 0 for those.
    bool is_wbfs = !memcmp(head,"WBFS",4);
    bool is_wdf  = !memcmp(head,"WDF\0",4);
    bool is_ciso = !memcmp(head,"CISO",4);
    bool is_iso  =  (head[0x18]==0x5d && head[0x19]==0x1c
		  && head[0x1a]==0x9e && head[0x1b]==0xa3)    // Wii raw
		  || (head[0x1c]==0xc2 && head[0x1d]==0x33
		   && head[0x1e]==0x9f && head[0x1f]==0x3d)    // GC raw
		  || (head[0x418]==0x5d && head[0x419]==0x1c
		   && head[0x41a]==0x9e && head[0x41b]==0xa3);   // XML disc
    // A disc claim requires the container header signature AND a disc file
    // extension.  Without the extension check, already-extracted disc
    // sub-files (e.g. the 1K "boot.bin" passport block, whose header carries
    // the same wii_magic at 0x18) would wrongly be re-dispatched to wit.
    bool is_disc = ( is_wbfs || is_wdf || is_ciso || is_iso ) && is_disc_ext(src);
    if ( is_disc || !memcmp(head,"NINTENDO",8) )	// DS ROM header claim
    {
	const bool ds = !memcmp(head,"NINTENDO",8);
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, ds, false, false, is_disc, false);
    }

    // 3DS NCCH / NCSD header signatures (strong pass):
    // NCCH at offset 0x100 (.cxi, .cfa, .app)
    // NCSD at offset 0x100 (.3ds, .cci)
    bool is_ncch = !memcmp(head+0x100,"NCCH",4);
    bool is_ncsd = !memcmp(head+0x100,"NCSD",4);
    if ( is_ncch || is_ncsd )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, false, true, false, false, false);

    // Switch NSP/XCI/NCA (strong pass):
    // PFS0 (offset 0), XCI "HEAD" tag (offset 0x100), and NCA magic (offset 0x200 or 0)
    bool is_nsp = !memcmp(head,"PFS0",4);
    bool is_xci = !memcmp(head+0x100,"HEAD",4);
    bool is_nca_sig = !memcmp(head+0x200,"NCA",3) || !memcmp(head,"NCA",3);
    if ( is_nsp || is_xci || is_nca_sig )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, false, false, false, false, true);

    // ----- claimed by extension alone (weak path only) -----

    // Nintendo DS ROM  (by extension)
    if ( !strong_only && is_ext(src,".nds") )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, true, false, false, false, false);

    // CIA / 3DS containers (by extension)
    if ( !strong_only && ( is_ext(src,".cia") || is_ext(src,".3ds")
			|| is_ext(src,".cci") || is_ext(src,".cxi") || is_ext(src,".cfa") ) )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, false, true, false, false, false);

    // Switch NCA / NSP / XCI (by extension)
    if ( !strong_only && ( is_ext(src,".nca") || is_ext(src,".nsp") || is_ext(src,".xci") ) )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, false, false, false, false, true);

    // Wii WAD (and the common 00000001.app content blob): both need a
    // header check, not just the extension. A ".app" is ambiguous -- it is
    // also the name sharpii itself gives to a WAD's *unpacked* IOS/title
    // content files, which are raw ELF/binary payloads with no WAD header
    // at all. Re-submitting those to sharpii (observed via a real WAD
    // round-trip: extracting a Photo Channel WAD produces 00000004.app/
    // 00000006.app/00000007.app, none of them WADs) fails loudly on every
    // one. A real WAD/boot2 header starts with a big-endian header size
    // (0x20 or 0x40) followed by the "Is\0\0"/"ib\0\0" type tag.
    bool is_wad_header = head[0]==0 && head[1]==0
	&& ( head[2]==0x00 && head[3]==0x20 || head[2]==0x00 && head[3]==0x40 )
	&& head[4]=='I' && (head[5]=='s' || head[5]=='b') && head[6]==0 && head[7]==0;
    if ( !strong_only && is_wad_header
	&& ( is_ext(src,".wad") || is_ext(src,".app") ) )
	return passthru_archive_or_bms(src,basedir,stage,
	    staged_dir,staged_dir_size, false, false, true, false, false);

    // Final fallback: nothing above claimed this file at all (not even a
    // recognized-but-tool-missing container) -- give the user's --bms=
    // script a shot at it before giving up.
    {
	const enumError bms_err = run_bms_fallback(strong_only,src,stage,staged_dir,staged_dir_size);
	if ( bms_err != ERR_NOTHING_TO_DO )
	    return bms_err;
    }

    return ERR_NOTHING_TO_DO;
}