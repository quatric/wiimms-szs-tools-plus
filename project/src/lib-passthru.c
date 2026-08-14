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
	fprintf(stdlog,"%s%sEXTRACT passport: %s -> %s (%s)\n",
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
	// This branch used to pass "--plaintext" -- a flag this ctrtool
	// (jakcron/Project_CTR) doesn't even have; the real flag is -p/
	// --plain, and it means the OPPOSITE of what the old name implied:
	// "extract data without decrypting", i.e. leave a retail CIA's NCCH
	// still encrypted. Confirmed against a real retail CIA (Tomodachi
	// Life): the tool silently no-op'd with the nonexistent flag before
	// (nonzero exit, no output), and *with* -p on real content it fails
	// downstream with ctrtool's own "NcchHeader is corrupted (Bad struct
	// magic)". Also, no output-directory flag was ever passed at all --
	// ctrtool defaults to a summary dump with nothing written to STAGE.
	// Fixed to omit -p (so it decrypts) and point --exefsdir/--romfsdir
	// at STAGE, verified to produce a normal recursible exefs/romfs tree.
	char exefs_dir[PATH_MAX], romfs_dir[PATH_MAX];
	snprintf(exefs_dir,sizeof(exefs_dir),"%s/exefs",stage);
	snprintf(romfs_dir,sizeof(romfs_dir),"%s/romfs",stage);
	(void)CreatePath(exefs_dir,false);
	(void)CreatePath(romfs_dir,false);

	char exefsdir_arg[PATH_MAX], romfsdir_arg[PATH_MAX];
	snprintf(exefsdir_arg,sizeof(exefsdir_arg),"--exefsdir=%s",exefs_dir);
	snprintf(romfsdir_arg,sizeof(romfsdir_arg),"--romfsdir=%s",romfs_dir);

	char *argv[] = {
	    (char*)tool,
	    exefsdir_arg,
	    romfsdir_arg,
	    (char*)src,
	    0
	};
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
	// hactool (SciresM) unpacks one container layer to --outdir, same
	// shape as ndstool/sharpii: NSP/XCI unpack to their member NCAs,
	// which this fork's own extract_tree() recursion then re-submits
	// here and hactool unpacks again as --type=nca (exefs/romfs/logo
	// land directly in outdir). NCA decryption needs a keyset hactool
	// finds on its own (~/.switch/prod.keys) -- not wired through here,
	// same as this project not shipping console key material anywhere
	// else. UNVERIFIED: no hactool binary or Switch sample was available
	// to test this against, unlike wit/ndstool/sharpii above (see
	// PLAN.md §3) -- flags are per hactool's own --help, not confirmed
	// against real output.
	ccp outdir_flag = is_ext(src,".nca") ? "--type=nca"
			: is_ext(src,".xci") ? "--type=xci"
			: "--type=pfs0"; // .nsp, or NSP claimed by PFS0 header
	char outdir_arg[PATH_MAX];
	snprintf(outdir_arg,sizeof(outdir_arg),"--outdir=%s",stage);
	char *argv[] = {
	    (char*)tool,
	    (char*)outdir_flag,
	    outdir_arg,
	    (char*)src,
	    0
	};
	const int rc = run_program(argv);
	if ( rc != 0 )
	    return ERROR0(ERR_SUBJOB_FAILED,
		"pass-through 'hactool %s' failed for %s (exit %d)",outdir_flag,src,rc);
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
	return passthru_archive(src,basedir,stage,
	    staged_dir,staged_dir_size, ds, false, false, is_disc, false);
    }

    // Switch NSP/XCI: PFS0 (offset 0) and the XCI "HEAD" tag (offset 0x100)
    // are real, plaintext container signatures (the encrypted NCA payload
    // inside is what needs a keyset, not the outer container), so these are
    // strong header claims like the disc/DS ones above -- not extension-only.
    bool is_nsp = !memcmp(head,"PFS0",4);
    bool is_xci = !memcmp(head+0x100,"HEAD",4);
    if ( is_nsp || is_xci )
	return passthru_archive(src,basedir,stage,
	    staged_dir,staged_dir_size, false, false, false, false, true);

    // ----- claimed by extension alone (weak path only) -----

    // Nintendo DS ROM  (by extension)
    if ( !strong_only && is_ext(src,".nds") )
	return passthru_archive(src,basedir,stage,
	    staged_dir,staged_dir_size, true, false, false, false, false);

    // CIA / 3DS containers
    if ( !strong_only && ( is_ext(src,".cia") || is_ext(src,".3ds")
			|| is_ext(src,".cci") || is_ext(src,".cxi") ) )
	return passthru_archive(src,basedir,stage,
	    staged_dir,staged_dir_size, false, true, false, false, false);

    // Switch NCA: the payload is encrypted, so there is no reliable
    // plaintext signature to key off -- extension-only, same tier as CIA.
    if ( !strong_only && is_ext(src,".nca") )
	return passthru_archive(src,basedir,stage,
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
	return passthru_archive(src,basedir,stage,
	    staged_dir,staged_dir_size, false, false, true, false, false);

    if ( !strong_only && opt_with_bms && *opt_with_bms )
    {
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
    }

    return ERR_NOTHING_TO_DO;
}