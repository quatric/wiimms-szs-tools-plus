// External pass-through for container formats added by the Nintendo fork.
// When XX/EXTRACT/XCOMMON encounter a file that is not a native archive or
// raw Nintendo codec stream (see lib-nintendo), it is either decoded in
// process or handed to an external tool (wit/ndstool/ctrtool/sharpii) that
// unpacks it into a staging directory under the extraction base dir.  The
// staged tree is then processed like any other extracted archive.
#ifndef SZS_LIB_PASSTHRU_H
#define SZS_LIB_PASSTHRU_H 1

#include "types.h"

// option controls, bound in tab-wszst.inc and CheckOptions() of wszst.c
extern bool opt_no_passthrough;		// --no-passthrough: disable pass-through
extern ccp opt_with_wit;		// --with-wit=path|name
extern ccp opt_with_ndstool;		// --with-ndstool=path|name
extern ccp opt_with_ctrtool;		// --with-ctrtool=path|name
extern ccp opt_with_sharpii;		// --with-sharpii=path|name
extern ccp opt_with_hactool;		// --with-hactool=path|name

// Try to pass an unrecognized SRC through to an external unpacker or to an
// in-process decoder.  STAGED_DIR is filled with the directory (relative to
// the caller's base dir) that received the unpacked payload when the return
// value is <= ERR_WARNING, so the caller can recurse into it.
//
// Return:
//   ERR_NOTHING_TO_DO  SRC is not a known pass-through container
//   ERR_WARNING        container recognized but the external tool is missing
//   ERR_OK             container recognized and unpacked; *STAGED_DIR is valid
//   > ERR_ERROR        container recognized but unpacking failed
enumError PassthruExtract ( ccp src, ccp basedir, char *staged_dir, uint staged_dir_size );

// Strong-signature variant of PassthruExtract(): claims only files whose
// HEADER identifies an external container (disc image magics, the DS
// "NINTENDO" tag).  Call this BEFORE the native archive/codec probes so a
// large disc image is dispatched to wit without tripping the file-size
// limited LoadFileAlloc() of the archive extractors; extension-only claims
// remain with PassthruExtract() so native decoders get first refusal.
enumError PassthruExtractStrong ( ccp src, ccp basedir, char *staged_dir, uint staged_dir_size );

#endif