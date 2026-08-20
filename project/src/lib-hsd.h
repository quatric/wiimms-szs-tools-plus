
/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "sysdolphin" (HSD) .dat archive support                 *
 *                                                                         *
 ***************************************************************************/

#ifndef LIB_HSD_H
#define LIB_HSD_H

#include "lib-std.h"
#include <stdint.h>
#include <stddef.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			HAL sysdolphin (HSD) DAT	///////////////
///////////////////////////////////////////////////////////////////////////////
//
// A sysdolphin ".dat" is not a directory-style container but a serialized
// object graph: a flat blob of C structs starting at file offset 0x20 whose
// inter-struct pointers are stored as offsets relative to that 0x20 data
// base and are patched to real addresses at load time using an explicit
// relocation table.  Used by Super Smash Bros. Melee, Kirby Air Ride and the
// Wii channel "Terebi no Tomo" / "TV no Tomo" (JPN).
//
// Header layout (big endian, ported from Ploaj/HSDLib, MIT licensed,
// HSDRaw/HSDRawFile.cs Open(), the relocation/root/reference parsing):
//
//	+0x00  u32  file size (== real file size)
//	+0x04  u32  relocation table offset, relative to the 0x20 data base
//	+0x08  u32  relocation entry count
//	+0x0C  u32  root node count
//	+0x10  u32  external reference count
//	+0x14  char[4] version string, e.g. "001B"
//	+0x20       data section (structs; all pointers are 0x20-relative)
//
// Each relocation entry is a 0x20-relative offset naming a *location* that
// holds a pointer; the u32 stored there is itself a 0x20-relative offset of
// the pointed-to struct.  After the relocation table come rootCount and
// refCount (offset,name-offset) pairs, followed by the string pool.
//
///////////////////////////////////////////////////////////////////////////////

#define HSD_DATA_BASE 0x20	// all stored offsets are relative to this

// [[hsd_t]]
typedef struct hsd_t
{
    const u8	*data;		// whole file, not owned
    uint	size;		// size of 'data'

    uint	fsize;		// header: file size
    uint	reloc_off;	// header: relocation table, absolute
    uint	n_reloc;	// header: number of relocation entries
    uint	n_root;		// header: number of root nodes
    uint	n_ref;		// header: number of external references
    char	version[5];	// header: version string, 0-terminated

    u32		*rel_src;	// 'n_rel' absolute pointer locations, sorted
    u32		*rel_dest;	// 'n_rel' absolute pointed-to offsets
    uint	n_rel;		// number of valid relocations

    u32		*target;	// sorted, unique list of pointed-to offsets
    uint	n_target;	// number of entries in 'target'
}
hsd_t;

///////////////////////////////////////////////////////////////////////////////

// Quick structural probe: HSD DAT files carry no magic, so this validates the
// header (file size, relocation table placement, root/reference counts and
// the ASCII version tag) instead.
bool IsHSD ( const u8 *data, uint size );

// Parse header + relocation table. Returns false if 'data' is not a HSD DAT.
// 'data' is borrowed and must outlive 'hsd'. Call ResetHSD() when done.
bool ScanHSD ( hsd_t *hsd, const u8 *data, uint size );
void ResetHSD ( hsd_t *hsd );

// Walk the object graph, decode every texture found and write it as a PNG
// into 'dest_dir', named "<basename>.tex###_<W>x<H>_<format>.png".
// Returns the number of textures written, or -1 on error.
int ExportHSDTextures
(
    const hsd_t	*hsd,		// valid, scanned HSD file
    ccp		dest_dir,	// output directory, created if needed
    ccp		basename	// name prefix for the written PNGs
);

// Convenience wrapper: scan 'data' and export its textures.
int ExportHSDTexturesFromData
	( const u8 *data, uint size, ccp dest_dir, ccp basename );

#endif // LIB_HSD_H
