# Wiimms SZS Tools

    *********************************
    *    _______ _______ _______    *
    *   |  ___  |____   |  ___  |   *
    *   | |   |_|    / /| |   |_|   *
    *   | |_____    / / | |_____    *
    *   |_____  |  / /  |_____  |   *
    *    _    | | / /    _    | |   *
    *   | |___| |/ /____| |___| |   *
    *   |_______|_______|_______|   *
    *                               *
    *       Wiimms SZS Tools        *
    *     https://szs.wiimm.de/     *
    *                               *
    *********************************
 
»Wiimms SZS Tools« is a set of command line tools to extract, modify and create
different files of game *Mario Kart Wii* and of other Nintendo games.
Development started in 2011. See https://szs.wiimm.de/ for more details,
documentation and downloads.

<dl>
<dt>Note:</dt>
<dd>
This is only a copy of Wiimms private SVN repository.
Only official releases are exported to <i>GitHub</i>.
Therefor merge requests can not imported directly and must be included manually.
</dd>

<dt>License:</dt>
<dd>
This program is free software;
you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

See file project/gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt for details.
</dd>
</dl>

*Wiimm, 2020-08-22*

---

## Fork: native Nintendo/Brawl format support

This fork adds native (no external binaries, except where noted) support for
a range of Wii/DS/3DS/Wii U/Switch container, texture, model and crypto
formats beyond what upstream Wiimm's SZS Tools ships, plus a few tools
(`wajpg`, `wlzh8`, `wbmsx`, `wwc24crypt`, `wlayt`, `wmdlt`, `wbrsar`) folded
into `wszst`/`wimgt`. Builds with the normal `cd project && make all -j4` and
is exercised by `tests/regress.sh`.

Full verification detail — what each format was checked against, which real
samples were used, and what remains unverified — is in a
[companion gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
rather than duplicated here.

### New features

- External pass-through extraction for containers with no native decoder —
  Wii/GC disc images, DS ROMs, CIA/3DS, Wii WADs, and Switch NSP/XCI/NCA are
  handed to `wit`/`ndstool`/`ctrtool`/`sharpii`/`hactool` and the unpacked
  tree is recursed into (`--no-passthrough` disables it).
- Wii U disc images (`.wud`/`.wux`) pass through to `wud2app`+`cdecrypt`
  (both required on PATH, plus a sibling `<name>.key` 16-byte title key next
  to the source). WUX is decompressed natively first (no external tool for
  that step); the disc is never read into RAM, so a 20+GB image extracts at
  the same fixed low memory cost as any other pass-through container.
- Recursive directory traversal and bottom-up incremental tree repacking:
  `wszst CREATE <wit-extracted-dir>` automatically detects modified assets (`.dae`, `.png`, `.yaml`, `.xml`, `.txt`) and child `.d` folders across the entire game hierarchy. It injects changed `.dae` models back into parent containers (`.brres`, `.bmd`, `.bch`, `.bcres`, `.bfres`, `.mdl0`), re-encodes textures and data, and rebuilds only the modified sub-archives (`.szs`, `.arc`, `.sarc`, `.narc`, `.darc`, `.pac`, `.gfa`, `.rarc`, `.bcsar`, `.bfsar`, `.rst`). Untouched sub-archives and files are skipped and preserved bit-for-bit.
- QuickBMS script chaining: `wszst xx --bms=<script.bms>` chains a QuickBMS
  script into `wszst xx` for unsupported containers, auto-staging and
  recursing into inner Nintendo assets.
- `wajpg`/`wlzh8`/`wbmsx`/`wwc24crypt`/`wlayt`/`wmdlt`/`wbrsar` folded into
  `wimgt`/`wszst` as first-class commands, no standalone binaries needed.
- `wszst xx` recurses into pass-through and SARC/PAC/GFA/DARC extraction
  output, auto-decoding textures, layouts, fonts, and models found inside.
- `CREATE`/`ENCODE` cache a SHA1 per member in a hidden `.wszst-cache.txt`
  next to `wszst-setup.txt`. A member whose current content still matches
  the cache is neither re-encoded (e.g. an untouched `.tpl.png`) nor does it
  force the archive to be reassembled and recompressed — only a real,
  content-level change pays that cost, and a fully unchanged directory
  leaves the destination file untouched instead of rewriting it byte-for-byte.

- Fixed Nintendo-format auto-detection (`DetectNintendoFormat`) and image
  loading (`wimgt DECODE`) for **WarioWare: D.I.Y. Showcase / "WarioWare
  Snapped!"** (DSiWare, NTR-KUWE). Every LZ11-compressed Nitro graphics
  resource it stores (NCGR/NCLR/NCER/NANR) is wrapped in an extra 4-byte
  little-endian size-prefix record *inside* the LZ11 stream, before the
  resource's own RGCN/RLCN/RECN/RNAN magic: byte 0 is always `0x00`, bytes
  1-2 are a `u16` LE holding `payload_size`, byte 3 is always `0x00`, and
  the real resource — magic and all — starts at offset 4. Confirmed against
  5 real assets pulled from the retail USA Rev1 ROM (`Style/StyleO.NCLR.bin`,
  `Style/Style_Head.NCGR.bin`, `Style/Style_Head.NCER.bin`,
  `Style/Style_2P_01.NANR.bin`, `Game/WarningB.NCLR.bin`): in every case
  bytes 1-2 as LE16 equalled `(decompressed size - 4)` exactly. Previously
  every one of these files reported as unrecognized (`?`) because detection
  only ever looked for the magic at offset 0. `DetectNintendoFormat` now
  recognizes the wrapped form directly; `LoadIMG`/`AssignIMG` auto-
  decompress LZ10/LZ11 Nitro graphics and peel off the wrapper before
  handing the payload to the existing NCGR/NCLR decoders (this also fixed a
  separate, unrelated pre-existing gap where LZ10/LZ11-compressed Nitro
  graphics were never auto-decompressed before decode at all). Verified
  end-to-end: a real `WarningO.NCGR.bin` from that ROM now decodes to a
  valid 128x160 PNG via `wimgt DECODE` where it previously hard-failed.
  One separate, still-open limitation found along the way and left
  untouched (out of scope for this fix): `DecodeNCLR_RGBA` rejects any
  `depth==3` (4bpp) palette with more than 16 total colour entries, but
  some real DSiWare NCLR files (e.g. `StyleO.NCLR.bin`, a 4-bank/64-colour
  character-customization palette) legitimately pack multiple 16-colour
  banks into one `depth==3` section — multi-bank palette support is a
  separate decoder enhancement, not part of this detection/wrapper fix.
- Fixed and validated **Monster Games RST/TOC + QuickLZ** container support
  (used by Excite Truck, ExciteBots: Trick Racing, NASCAR Heat) against a real
  retail disc. TOC entry offsets are relative to the uncompressed payload,
  not the outer RST header; creation and extraction now use that retail
  convention instead of a mutually compatible 128-byte absolute-offset bug.
- Added native decode of the Monster Games (Excite Truck / ExciteBots)
  asset formats found inside those RST/TOC archives: `.tex` GX textures and
  `.art`/`.img` GUI images (both -> PNG, see the ART/IMG and TEX table rows
  for how the pixel format is recovered without a stored format field),
  `.msh` structured PMsh collision resources (-> COLLADA DAE), and `.mod`
  (NDL3/NDL2 GX display-list models, geometry format read straight out of
  the embedded display list's VAT register writes and a brute-forced index byte width) -> GLB, validated
  on 135/135 real Excite Truck and 193/203 real ExciteBots samples (see the
  MOD row). `.tex`/`.art`/`.msh`/`.mod` all decode real retail samples
  correctly and are safe to use today.

See the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for the full history of what was fixed, how each format was verified, and
against which real samples — not duplicated here.

### Format & compression support

3D model export defaults to **GLB** (binary glTF, textures embedded in the
file) rather than COLLADA/DAE — `wszst XX`/`XEXPORT`/`wmdlt ENCODE` write
`.glb` unless `--dest` names a file that explicitly ends in `.dae`, in which
case DAE (with loose sibling PNGs, still supported for injection workflows)
is written instead. Notes below that say "encode via DAE `--parent`
injection" describe the injection path specifically, which still consumes a
`.dae`; the model-export *default* is GLB.

| Format | Category | Decode | Encode | Notes |
|---|---|---|---|---|
| AJJPG / AJPG | Still image | ✅ | ✅ | GBA-era still image container |
| ASH0 | Compression | ✅ | ✅ | |
| AT7 | Archive/compression | ✅ | ✅ | Another Century's Episode / Koei Tecmo |
| BCFNT | Font | ✅ | ✅ | 3DS bitmap font; full pixel decode to PNG via `wimgt`/`wszst XX` (RGBA8 linear exact; I4/I8/IA4/IA8/RGB565 via GX tile path, correct for linear data), encode via `wimgt` |
| BCH | Model | ✅ | ✅ | 3DS CTR H3D, incl. geometry; encode via DAE `--parent` injection |
| BCLAN | Layout | ✅ | ✅ | 3DS layout animation; shares BCLYT's parser/encoder, same status |
| BCLIM | Texture | ✅ | ✅ | 3DS textures |
| BCLYT | Layout | ✅ | ✅ | 3DS layout; 1980/1980 real files decode AND byte-exact round-trip (decode→encode→decode) against a real cartridge dump |
| BCRES | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| BCSAR | Audio archive | ✅ | ✅ | 3DS Sound Archive (CSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`) |
| BCWAV | Audio track | ✅ | ⛔ | 3DS Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV |
| BCWAR | Audio archive | ✅ | ✅ | 3DS Sound Wave Archive (CWAR); unpacks member BCWAV audio tracks and repacks (`wszst CREATE`) |
| ART / IMG | Texture | ✅ | 🟡 | Excite Truck / ExciteBots (Wii) GUI images; raw GX pixel data, single mip level, zeroed footer — dimensions/format recovered via GX tile-seam continuity; colour+stencil pairs (stacked as one double-height decode) are detected and recombined into one proper RGBA image. Encode (`wimgt CONVERT` → `.art`/`.img`) picks a GX format and self-verifies by decoding its own output back through the real classifier, retrying candidates until one recovers the exact dimensions/format and matching pixels, or fails loudly rather than risk silently shipping a wrong image; verified 84/94 decodable retail files round-trip exactly or near-exactly |
| BCGRP | Audio archive | ✅ | ✅ | 3DS Sound Group Archive (CGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
| BFFNT | Font | ✅ | ✅ | Wii U bitmap font; full pixel decode to PNG via `wimgt`/`wszst XX` (RGBA8 linear exact; I4/I8/IA4/IA8/RGB565 via GX tile path, correct for linear data), encode via `wimgt` |
| BFLAN | Layout | ✅ | ✅ | Wii U layout animation; lossless semantic text roundtrip via `wlayt`, verified on 1,148/1,148 retail files |
| BFLIM | Texture | ✅ | ✅ | Wii U textures, incl. BC1/BC2/BC3/BC4/BC5 block-compressed formats (fmt 14-17, 21-23) |
| BFLYT | Layout | ✅ | ✅ | Wii U layout; platform-specific material, pane, text, parts, group and container structures; lossless semantic text roundtrip via `wlayt`, verified on 251/251 retail files |
| BFRES | Model | 🟢 | ⛔ | Switch; geometry decode (position/normal/UV, first LOD mesh) to DAE verified against real Super Mario Odyssey retail data (v8+v9); falls back to the names/shapes/materials-only structure XML for the rare shape it can't decode yet |
| BFRES | Model | ✅ | ✅ | Wii U; encode via DAE `--parent` injection; FMAT materials bound to their first FTEX texture ref, decoded+PNG'd during extraction (98.8% of a real disc's models resolve a diffuse texture) |
| BFSAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Archive (FSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`) |
| BFWAV | Audio track | ✅ | ⛔ | Wii U / Switch Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV |
| BFWAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Wave Archive (FWAR); unpacks member BFWAV audio tracks and repacks (`wszst CREATE`) |
| RWAV | Audio track | ✅ | ✅ | Wii Sound Wave (the individual sample RWAR wave archives and RBNK instrument banks reference); DSP-ADPCM and PCM16/PCM8 to/from WAV via `wrwav`, container/codec logic ported from BrawlLib's real RWAV parser (soopercool101/BrawlCrate) |
| BFGRP | Audio archive | ✅ | ✅ | Wii U / Switch Sound Group Archive (FGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
| BRSTM / BFSTM / BCSTM | Audio stream | ✅ | ✅ | Wii/Wii U/3DS sound stream (`wbrstm`); DSP-ADPCM (`adpcm_thp`) and PCM16 to/from WAV. Encoding (`from_wav`) prefers passing straight through to the sibling 'mobipeg' repo's real `adpcm_thp` encoder over this project's own port, falling back to the port when mobipeg isn't installed or predates the brstm/dsp/bns muxers |
| BLZ | Compression | ✅ | ✅ | DS ARM9/ARM7/overlay compression |
| BMD (Early DS) | Model | ✅ | ✅ | Super Mario 64 DS / early Nitro 3D models; geometry decode to DAE & encode via DAE `--parent` injection |
| BMS | Interpreter | ✅ | 🟡 | QuickBMS interpreter (`wbmsx` + `wszst xx --bms`); native codec aliases only |
| BNTX | Texture | 🟡 | ✅ | Switch textures; RGBA8/565/5551/4 + BC1-5 + ASTC_4x4 decode, RGBA8 encode; BC6H/BC7/other ASTC block sizes not seen in real samples yet, unimplemented |
| BREFT | Texture | ✅ | ✅ | Brawl effect texture, palette-indexed; encode via `wszst CREATE --breft`, `wimgt --btimg` |
| BRFNA | Font | ✅ | ✅ | Wii font archive, RFNA; encode via `wimgt ENCODE .brfna` |
| BRFNT | Font | ✅ | ✅ | Wii bitmap font; encode via `wimgt ENCODE .brfnt` |
| BRLAN | Layout | ✅ | ✅ | Wii layout animation; lossless text roundtrip via `wlayt` |
| BRLYT | Layout | ✅ | ✅ | Wii layout; lossless text roundtrip via `wlayt` |
| BRRES MDL0 | Model | ✅ | ✅ | Wii models → COLLADA; encode via DAE `--parent` injection |
| BRRES TEX0 | Texture | ✅ | ✅ | Wii textures; encode via `wimgt`; palette-indexed decode pairs sibling PLT0 data |
| BRSAR | Audio | ✅ | ⛔ | → MIDI+SF2 (`wbrsar`) |
| BYAML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| BYML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| CCF | Archive | ✅ | ✅ | Wii/Switch Virtual Console archive, optional zlib compression; create via `wszst CREATE .ccf` |
| CGFX | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| CTPK | Texture / Archive | ✅ | ✅ | 3DS multi-texture container; encode via `wimgt` or folder create via `wszst CREATE .ctpk` |
| DARC | Archive | ✅ | ✅ | 3DS "differential archive" container |
| Deflate | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .deflate` |
| GFA | Archive | ✅ | ✅ | "GFAC" archive; create via `wszst CREATE .gfa` |
| GTX | Texture | ✅ | ✅ | Wii U GX2 "Gfx2" texture container. Lossless bounds-checked parsing and raw-element encoding cover the complete public `GX2SurfaceFormat` storage matrix, explicit mip levels, array/cube/3D slices, MSAA samples, component selectors, depth/stencil surfaces, tile modes 0-15, thick tiles, bank swaps, and pipe/bank/slice/sample rotation. RGBA visualization covers UNORM/UINT/SNORM/SINT/FLOAT/SRGB channel formats, packed RGB/RGBA and depth formats, plus BC1-5 (including signed BC4/BC5); ordinary RGBA input and caller-supplied compressed/raw elements can be tiled back into GTX. Dedicated regressions exercise all public formats, every tile mode, mipmaps, arrays and 4x MSAA. |
| GSH | Shader | 🟡 | 🟡 | Shader-only Wii U Gfx2 container; lossless bounds-checked blocks, relocation/string-table metadata, vertex/pixel/geometry/compute header↔program associations, and unknown blocks retained. `wszst EXTRACT file.gsh` emits named, losslessly reassemblable Latte control-flow listings. Full ALU/TEX/VTX operand pretty-printing and high-level GLSL compilation remain delegated to Latte Assembler/CafeGLSL rather than misrepresented as texture-container functionality. |
| HSF (HSFV037) | Model | 🟡 | 🟡 | HAL/Hudson SysDolphin model (Mario Party 4-8 `.hsf`, extracted from MPBIN); exports geometry (including vertex colors, float/packed normals, UVs, triangles/quads/indexed strips), hierarchy, CENV skinning/inverse binds, per-primitive materials, GX textures/palettes, recursively nested replica mesh subtrees (shared geometry, cycle guards, material splits, and composed transforms), shape and partial-vertex cluster morph targets, exact type-2 successive-lerp cluster coefficients, native perspective cameras, `KHR_lights_punctual` lights, and native 60 Hz glTF TRS/morph animations (step/linear/Bézier/constant HSF curves) to GLB; DAE receives the static model. Lossless sidecars retain motions, replicas, camera/light parameters, fog scenes, map attributes, matrix tables, parts, clusters, and shapes, including metadata-only HSF files. `wmdlt ENCODE model.dae --dest model.hsf` writes big-endian HSFV037 geometry, normals, UVs, vertex colors, triangle material assignments, materials, joint/object hierarchy, material attribute/symbol chains, and PNG layers converted to native GX RGBA8 textures, with textured encode→decode regression coverage. The model API additionally emits shape morph buffers/weights, provenance-marked cluster morphs as explicit HSF parts/clusters, recursive-ready replica records with decomposed transforms, native camera/light records, whole/single/multi-weight CENV envelopes, and sampled linear TRS/morph motion tracks (including quaternion-to-Hudson-Euler conversion). Indexed palette output is normalized to lossless RGBA8 rather than preserving a source palette encoding. Validated on MP4 Mario and 7,284 MP7 retail HSFs, including dedicated nested-replica/cluster/shape/camera/light fixtures. |
| Huffman 0x24 | Compression | ✅ | ✅ | 4-bit nibble |
| Huffman 0x28 | Compression | ✅ | ✅ | 8-bit byte |
| Mario Party `.bin` | Archive | ✅ | ✅ | MPBIN container, games 4-8; unpacks & repacks via `wszst CREATE .bin` |
| MOD (NDL3/NDL2) | Model | ✅ | ⛔ | Monster Games Excite Truck / ExciteBots (Wii) `.mod` 3D model; geometry format (position/UV element count, numeric format, fixed-point shift, index width) is read directly out of the embedded GX display list's vertex-attribute-table register writes rather than hardcoded, so it decodes the general case, not one fixed shape -> GLB (or COLLADA DAE with `--dest *.dae`); validated on 135/135 real Excite Truck and 193/203 real ExciteBots samples (the gap is exactly the separate .msh collision-mesh files, not .mod failures) |
| MSH (PMsh) | Model | ✅ | ⛔ | Monster Games Excite Truck / ExciteBots (Wii) collision resource: little-endian bucket, indexed-position, triangle-normal and collision-plane records -> COLLADA DAE; layout recovered from the retail game loader/raycast code and validated on all 7 real ExciteBots samples, including `gpmesh.msh` and `rail2bp.msh` |
| MSBF | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Flow; decode & encode via `wbmgt` & `wszst` |
| MSBP | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Project; decode & encode via `wbmgt` & `wszst` |
| MSBT | Text/flow | ✅ | ✅ | Nintendo Message Studio Binary Text; decode & encode via `wbmgt` & `wszst` |
| NANR | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NARC | Archive | ✅ | ✅ | Nitro Archive, DS/3DS container; create via `wszst CREATE .narc` |
| NCER | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NCGR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCLR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCCARC | Archive | ✅ | ✅ | WarioWare: Touched! (DS) flat blob container; unpacks member chunks and repacks via `wszst CREATE .nccarc` |
| NSBMD | Model | ✅ | ✅ | DS models, incl. bone hierarchy; encode via DAE `--parent` injection |
| ODH | Still image | ✅ | ✅ | GBA-era still image codec |
| PAC | Archive | ✅ | ✅ | Brawl "ARC\0" archive; create via `wszst CREATE .pac` |
| PLT0 | Animation | ✅ | ✅ | Brawl G3D palette-swap animation; IA8, RGB565, RGB5A3 encode via `wimgt` |
| PSDK | Unknown | 🔍 | ⛔ | detected, not decoded |
| QuickLZ | Compression | ✅ | ✅ | both stream versions (1.20, 1.4.0) |
| RARC | Archive | ✅ | ✅ | GameCube / Wii object archive; create via `wszst CREATE .rarc` |
| romc | Compression | ✅ | ⛔ | N64 Virtual Console ROM compression; not every N64 VC title uses it (verified: Yoshi's Story stores its ROM raw, Kirby 64 uses this) |
| RL | Compression | ✅ | ✅ | |
| RNC1 | Compression | ✅ | ⛔ | |
| RNC2 | Compression | ✅ | ✅ | encode via `wszst COMPRESS --dest .rnc` |
| RSEQ | Sequence | ✅ | ✅ | Wii Revolution Sequence (.rseq/.brseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| CSEQ | Sequence | ✅ | ✅ | 3DS CTR Sequence (.cseq/.bcseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| FSEQ | Sequence | ✅ | ✅ | Wii U & Switch Format Sequence (.fseq/.bfseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| SSEQ | Sequence | ✅ | ✅ | Nintendo DS Nitro Sequence (.sseq); MML disassembly, assembly, MIDI conversion (`wseqt` & `wszst`) |
| SDAT | Audio archive | ✅ | ⛔ | Nintendo DS Sound Archive; MIDI + SoundFont SF2 extraction via `wbrsar` |
| TEX | Texture | 🟡 | 🟡 | Excite Truck / ExciteBots (Wii) GX texture; raw GX pixel data with no stored pixel format, only a 24-byte dimension footer — format recovered by decoding every plausible GX format and keeping the one whose mip level 1 is a correct 2x box-downsample of level 0. Encode (`wimgt CONVERT` → `.etex`, rename to `.tex` for use with `wszst`/the game) self-verifies the same way as ART, since the header-less format means many (format,width,height,mip-count) combinations can alias to the exact same file size; verified 2,723/3,151 decodable retail files (86%) round-trip exactly or near-exactly, with the rest failing loudly (never silently wrong) when no candidate format survives the round trip |
| WARC | Archive | ✅ | ⛔ | Game & Wario (Wii U) flat archive; big-endian, uncompressed, unrelated to Excite's TOC/RES despite the naming coincidence; ported from aluigi's `game_wario.bms` |
| WC24 crypto | Crypto | ✅ | ✅ | `wwc24crypt` |
| WUD | Disc image | ✅ | ✅ | Wii U disc image; pass-through via `wud2app`+`cdecrypt` |
| WUX | Disc image | ✅ | ✅ | Wii U disc image, compressed; native WUX compress & decompress |
| Yay0 | Compression | ✅ | ✅ | |
| Zlib | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .zlib` |

✅ supported · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for verification details and real samples used for each of these.

### Formats WSZST supports officially (stock, upstream)

The table above covers this fork's own additions. Everything below is the
format list stock/upstream `wszst` already ships (`wszst FILETYPE`), listed
here for comparison. Status is read directly from each format's
`ff_attrib_t` flags in `src/file-type.c` (`FFT_DECODE`/`FFT_EXTRACT`/
`FFT_COMPRESS` → Decode, `FFT_ENCODE`/`FFT_CREATE`/`FFT_COMPRESS` → Encode).

| Format | Decode | Encode |
|---|---|---|
| ARC (RARC container) | ✅ | ✅ |
| BMG | ✅ | ✅ |
| BMG-TXT | ✅ | ✅ |
| BRASD | ⛔ | ⛔ |
| BREFF | ✅ | ✅ |
| BREFT | ✅ | ✅ |
| BREFT-IMG | ✅ | ✅ |
| BRES (BRRES) | ✅ | ✅ |
| BTI | ✅ | ✅ |
| BZ | ✅ | ✅ |
| BZ2 | ✅ | ✅ |
| C0CODE | ⛔ | ⛔ |
| C0DATA | ⛔ | ⛔ |
| C1CODE | ⛔ | ⛔ |
| C1DATA (CTCODE) | ✅ | ⛔ |
| CHR (CHR0) | ⛔ | ⛔ |
| CLR (CLR0) | ⛔ | ⛔ |
| CRS1 | ⛔ | ⛔ |
| CT-DEF | ✅ | ✅ |
| CT-SHA1 | ⛔ | ⛔ |
| CUP1 | ⛔ | ⛔ |
| CUPICON (cup-icon TPL) | ⛔ | ✅ |
| DOL | ✅ | ⛔ |
| DRIVER | ⛔ | ⛔ |
| GCH | ⛔ | ⛔ |
| GCT | ⛔ | ⛔ |
| GCT-TXT | ⛔ | ⛔ |
| GH-IOBJ | ✅ | ✅ |
| GH-IOBJ-TXT | ✅ | ✅ |
| GH-ITEM | ✅ | ✅ |
| GH-ITEM-TXT | ✅ | ✅ |
| GH-KART | ✅ | ✅ |
| GH-KART-TXT | ✅ | ✅ |
| GH-KOBJ | ✅ | ✅ |
| GH-KOBJ-TXT | ✅ | ✅ |
| ITEMSLT | ⛔ | ⛔ |
| ITEMSLT-TXT | ⛔ | ⛔ |
| KCL | ✅ | ✅ |
| KCL-TXT | ✅ | ✅ |
| KMG | ✅ | ✅ |
| KMG-TXT | ✅ | ✅ |
| KMP | ✅ | ✅ |
| KMP-TXT | ✅ | ✅ |
| KRM | ⛔ | ⛔ |
| KRM-TXT | ⛔ | ⛔ |
| KRT | ⛔ | ⛔ |
| KRT-TXT | ⛔ | ⛔ |
| LE-BIN | ✅ | ⛔ |
| LE-DEF | ✅ | ✅ |
| LE-DIS | ✅ | ✅ |
| LE-REF | ✅ | ✅ |
| LE-STR | ✅ | ✅ |
| LEX | ✅ | ✅ |
| LEX-TXT | ✅ | ✅ |
| LFL | ✅ | ✅ |
| LPAR | ✅ | ✅ |
| LTA | ✅ | ✅ |
| LZ | ✅ | ✅ |
| LZMA | ✅ | ✅ |
| MDL (MDL0) | ✅ | ⛔ |
| MDL-TXT | ⛔ | ⛔ |
| MOD1 | ⛔ | ⛔ |
| MOD2 | ⛔ | ⛔ |
| MTCAT | ✅ | ✅ |
| OBFLOW (OBJFLOW) | ✅ | ✅ |
| OBJFLOW-TXT | ✅ | ✅ |
| OVR1 | ⛔ | ⛔ |
| PACK | ✅ | ✅ |
| PAT (PAT0) | ✅ | ✅ |
| PAT-TXT | ✅ | ✅ |
| PNG | ✅ | ✅ |
| PREFIX | ✅ | ✅ |
| REL (STATICR) | ⛔ | ⛔ |
| RKC | ✅ | ✅ |
| RKCO | ⛔ | ⛔ |
| RKG | ⛔ | ⛔ |
| SCN (SCN0) | ⛔ | ⛔ |
| SHA1ID | ⛔ | ✅ |
| SHA1REF | ⛔ | ✅ |
| SHP (SHP0) | ⛔ | ⛔ |
| SKP-OBJ | ✅ | ✅ |
| SRT (SRT0) | ⛔ | ⛔ |
| TEX (TEX0) | ✅ | ✅ |
| TEX+CT | ✅ | ✅ |
| TPL | ✅ | ✅ |
| U8 | ✅ | ✅ |
| VEH (VEHICLE) | ⛔ | ⛔ |
| WAV-OBJ | ✅ | ✅ |
| WCH | ⛔ | ⛔ |
| WPF | ⛔ | ⛔ |
| WU8 | ✅ | ✅ |
| XPF | ⛔ | ⛔ |
| XYZ | ✅ | ✅ |
| XZ | ✅ | ✅ |
| YAZ (YAZ0) | ✅ | ✅ |
| YAZ1 | ✅ | ✅ |
| YBZ | ✅ | ✅ |
| YLZ | ✅ | ✅ |

✅ supported · ⛔ not implemented/registered — many of these `⛔` rows (e.g.
`CHR0`/`CLR0`/`SCN0`/`SHP0`/`SRT0`, the `RKG`/`GCT`/`WCH` family) are
recognized/detected file types with no dedicated decode or encode path
wired up in stock `wszst` itself.
