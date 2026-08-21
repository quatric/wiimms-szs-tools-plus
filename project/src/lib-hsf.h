// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// HAL Laboratory "HSFV037" model format (Mario Party 4-8 .hsf, extracted from
// the MPBIN container by wmpbdump; the same tool family HAL used across
// Kirby Air Ride, Battalion Wars and other GameCube/Wii titles).
//
// See the comment above DecodeHSF() in lib-hsf.c for the AttributeHeader
// table layout and what is/isn't decoded.
//-----------------------------------------------------------------------------

#ifndef LIB_HSF_H
#define LIB_HSF_H

#include "types.h"

// Recognise and decode an HSFV037 model's geometry -- positions, normals,
// UVs and triangle/quad primitives, one mesh per named mesh-object, single
// part or multi-part alike -- to a COLLADA .dae beside 'out_dae_path'.
// Materials, textures and bone/skin-weight rigging are out of scope (every
// mesh is exported unbound, in file-local bind pose). Returns
// ERR_NOTHING_TO_DO if 'data' isn't an HSFV037 file, or if it uses a
// primitive record type other than triangle/quad that this decoder does
// not yet support -- see lib-hsf.c.
enumError DecodeHSF ( const u8 *data, uint size, ccp out_dae_path );

#endif // LIB_HSF_H
