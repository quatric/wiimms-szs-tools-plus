// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// HAL Laboratory "HSFV037" model format (Mario Party 4-8 .hsf, extracted from
// the MPBIN container by wmpbdump; the same tool family HAL used across
// Kirby Air Ride, Battalion Wars and other GameCube/Wii titles).
//
// See the comment above DecodeHSF() in lib-hsf.c for the section-table
// layout and what is/isn't decoded.
//-----------------------------------------------------------------------------

#ifndef LIB_HSF_H
#define LIB_HSF_H

#include "types.h"

// Recognise and decode an HSFV037 model's static geometry (single-block
// position/face sections only -- see lib-hsf.c) to a COLLADA .dae beside
// 'out_dae_path'. Returns ERR_NOTHING_TO_DO if 'data' isn't an HSFV037 file,
// or if its geometry sections use the multi-block layout (skinned/multi-part
// models) that this decoder does not yet support.
enumError DecodeHSF ( const u8 *data, uint size, ccp out_dae_path );

#endif // LIB_HSF_H
