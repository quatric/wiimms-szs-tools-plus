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
- Recursive directory traversal for CLI file args via a `**` glob, e.g.
  `wszst DECOMPRESS 'somedir/**/*.ext'`.
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
- Fixed a real bug in stock BMG text decode/encode: message text
  (`BMG_ENC_UTF16BE`) was read/written using the container's own structural
  endianness instead of always big-endian, silently producing unreadable
  text for any BMG whose structural fields are little-endian while its text
  stays big-endian (a real, common combination — e.g. the Wii System Menu's
  own message files). The bug was round-trip-clean (decode+encode both
  applied the same wrong transformation), so it only showed up as garbled
  output, never a hard error.
- BFLIM decode gained BC1/BC2/BC3/BC4/BC5 block-compressed formats (fmt
  14-17, 21-23), reusing the already-verified BNTX block decoders. Found by
  a real full-disc validation run (Splatoon USA): 2125 of 2133 real BFLIM
  files on that one disc used these formats and previously hard-failed with
  `ERROR #38 [INVALID IMAGE FORMAT]`; all 2133 now decode.
- Found and fixed a real, previously-unnoticed correctness bug: BFLYT (Wii U)
  does NOT share BCLYT's (3DS) struct layout, despite this fork's own
  assumption otherwise. `pan1` (the shared pane base every `pic1`/`txt1`/
  `wnd1`/`bnd1`/`prt1` builds on) has an extra 8-byte "user data" field on
  Wii U that doesn't exist on 3DS, misaligning every field after it — every
  translation/rotation/scale/width/height/material-index/color on every real
  Wii U pane. `lyt1` and `grp1` are entirely different structs, not just
  different field widths. All three fixed and verified against two real
  retail Wii U games (Splatoon, Super Mario Maker), platform-gated on the
  FLYT/FLAN vs CLYT/CLAN magic so the already-verified 3DS path is
  untouched. Also fixed `mat1` (Wii U's real material struct: 28-byte name,
  separate foreground/background colors, and a flags bitfield with
  different bit widths than 3DS's — cross-checked against
  Tyulis/3DSkit's BFLYT.md and byte-accounting on real files down to
  individual bits, e.g. the documented "2-bit blend-mode count" turned out
  to actually be a single presence bit). Also fixed `prt1` (3 offset
  fields per sub-pane entry, not 2 — confirmed on a real entry with all 3
  simultaneously non-zero and distinct) and `txt1`, which turned out to be
  the biggest single win: its text and call-name were being read from the
  wrong position entirely (sequentially after the fixed header, landing on
  color/font-size bytes) instead of following their own offset pointers
  (verified against real decoded text, e.g. a "Camera Sensitivity" label
  in Japanese resolving correctly), and its material/font index fields
  silently reject a real `0xFFFF` "no override" sentinel value that real
  files actually use. Real BFLYT files going from effectively 0% parseable
  (`lyt1` alone hard-failed nearly every file) to **506/561 (90%)** on
  this corpus (decode only; encoders are still 3DS-shaped for `mat1`).
  `cnt1` (20 files) is the only section left unexamined.
- Switch BFRES gained real geometry decode (position/normal/UV, first LOD
  mesh per shape) to DAE, on top of the already-verified name-resolution
  manifest. Switch BFRES uses a completely different layout from Wii U
  BFRES despite the shared "FRES" magic: little-endian throughout (vs
  Wii U's big-endian), absolute 8-byte pointers (vs Wii U's self-relative
  offsets), and a single file-wide `BufferInfo` memory pool that every
  shape's vertex/index buffers are packed into sequentially rather than
  each shape owning its own buffer. Verified byte-for-byte against
  KillzXGaming/BfresLibrary's C# reference implementation and a real
  Super Mario Odyssey retail sample (`AirBubble.bfres`: 700 vertices/520
  faces, matching the source exactly) before being run across 300 random
  `.szs` files from the full Odyssey RomFS — 100% of files that actually
  contain shape geometry (234/234, the other 40/274 BFRES files in the
  sample are texture- or animation-only with no mesh data at all) produced
  a valid DAE. One real bug found along the way: `VertexAttrib.Format` is
  explicitly read big-endian in the reference decoder (a documented
  per-field override), which is correct only for that one field — a first
  pass wrongly applied the same swap to `Mesh.PrimitiveType`/`IndexFormat`,
  which the reference reads as plain little-endian like everything else,
  and that one field mixup was the entire difference between "decodes
  nothing" and "decodes correctly."
- Added **NCCARC** (WarioWare: Touched! DS), a container format with no
  magic and no prior public documentation — the only prior art found is a
  single unanswered 2017 forum thread asking what it even is. Reverse-
  engineered from scratch by byte-accounting on 307 real `cg_*.nccarc`
  files pulled from a real cartridge dump: a flat table of little-endian
  u32 offsets starting at file offset 0 with no explicit count field (the
  table's own byte length equals its first entry, giving the count for
  free), whose last entry always equals the file size as a terminator.
  Some entries have bit 31 set as a per-chunk flag of unknown meaning;
  masking it off always restores a value that fits monotonically between
  its neighbours. This invariant held byte-exact on 305/305 non-empty real
  samples across the whole game. Only the container's own member
  boundaries are split out (`wszst EXTRACT foo.nccarc`) — what's actually
  inside each chunk (raw tile/palette data vs. whole-screen illustrations,
  judging by the wide size variance) is not decoded, same scope this
  fork's PAC/GFA support started at before their contents were understood.

See the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for the full history of what was fixed, how each format was verified, and
against which real samples — not duplicated here.

### Format & compression support

| Format | Category | Decode | Encode | Notes |
|---|---|---|---|---|
| AJPG | Still image | ✅ | ✅ | GBA-era still image container |
| ASH0 | Compression | ✅ | ✅ | |
| AT7 | Archive/compression | ✅ | ✅ | Another Century's Episode / Koei Tecmo |
| BCFNT | Font | 🟡 | ✅ | 3DS bitmap font; structure/TGLP decode, encode via `wimgt` |
| BCH | Model | ✅ | ✅ | 3DS CTR H3D, incl. geometry; encode via DAE `--parent` injection |
| BCLAN | Layout | 🟡 | 🟡 | 3DS layout animation; shares BCLYT's parser/encoder, same status |
| BCLIM | Texture | ✅ | ✅ | 3DS textures |
| BCLYT | Layout | 🟡 | 🟡 | 3DS layout; 1980/1980 real files parse; known `txt1` field gap |
| BCRES | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| BFFNT | Font | 🟡 | ✅ | Wii U bitmap font; structure/TGLP decode, encode via `wimgt` |
| BFLAN | Layout | 🟡 | 🟡 | Wii U layout animation; shares BCLYT's parser/encoder for its own sections — not independently checked for the BFLYT-vs-BCLYT struct divergence found 2026-08-15 |
| BFLIM | Texture | ✅ | ✅ | Wii U textures, incl. BC1/BC2/BC3/BC4/BC5 block-compressed formats (fmt 14-17, 21-23) |
| BFLYT | Layout | 🟡 | 🟡 | Wii U layout; does NOT share BCLYT's struct layout (correction — see below); pan1/lyt1/grp1/mat1/prt1/txt1 fixed for real Wii U files, 506/561 (90%) real files fully parse (decode only — encoders still 3DS-shaped); cnt1 remains unexamined |
| BFRES | Model | 🟢 | ⛔ | Switch; geometry decode (position/normal/UV, first LOD mesh) to DAE verified against real Super Mario Odyssey retail data (v8+v9); falls back to the names/shapes/materials-only structure XML for the rare shape it can't decode yet |
| BFRES | Model | ✅ | ✅ | Wii U; encode via DAE `--parent` injection |
| BLZ | Compression | ✅ | ✅ | DS ARM9/ARM7/overlay compression |
| BMS | Interpreter | ✅ | 🟡 | QuickBMS interpreter (`wbmsx` + `wszst xx --bms`); native codec aliases only |
| BNTX | Texture | 🟡 | ✅ | Switch textures; RGBA8/565/5551/4 + BC1-5 + ASTC_4x4 decode, RGBA8 encode; BC6H/BC7/other ASTC block sizes not seen in real samples yet, unimplemented |
| BREFT | Texture | ✅ | ✅ | Brawl effect texture, palette-indexed; encode via `wszst CREATE --breft`, `wimgt --btimg` |
| BRFNA | Font | ✅ | ✅ | Wii font archive, RFNA; encode via `wimgt ENCODE .brfna` |
| BRFNT | Font | ✅ | ✅ | Wii bitmap font; encode via `wimgt ENCODE .brfnt` |
| BRLAN | Layout | ✅ | ✅ | Wii layout animation; lossless text roundtrip via `wlayt` |
| BRLYT | Layout | ✅ | ✅ | Wii layout; lossless text roundtrip via `wlayt` |
| BRRES MDL0 | Model | ✅ | ✅ | Wii models → COLLADA; encode via DAE `--parent` injection |
| BRRES TEX0 | Texture | ✅ | ⛔ | Wii textures; palette pairing w/ PLT0 |
| BRSAR | Audio | ✅ | ⛔ | → MIDI+SF2 (`wbrsar`) |
| BYAML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| BYML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| CGFX | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| CTPK | Texture | ✅ | ✅ | 3DS texture container |
| DARC | Archive | ✅ | ✅ | 3DS "differential archive" container |
| Deflate | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .deflate` |
| GFA | Archive | ✅ | ✅ | "GFAC" archive; create via `wszst CREATE .gfa` |
| Huffman 0x24 | Compression | ✅ | ✅ | 4-bit nibble |
| Huffman 0x28 | Compression | ✅ | ✅ | 8-bit byte |
| Mario Party `.bin` | Archive | ✅ | ✅ | MPBIN container, games 4-8 |
| NANR | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NARC | Archive | ✅ | ✅ | Nitro Archive, DS/3DS container |
| NCER | Sprite | ✅ | ✅ | DS sprite; XML via `wszst CREATE` |
| NCGR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCLR | Sprite | ✅ | ✅ | DS sprite; via `wimgt` |
| NCCARC | Archive | ✅ | ⛔ | WarioWare: Touched! (DS) undocumented flat blob container; splits into member chunks, chunk contents themselves not decoded |
| NSBMD | Model | ✅ | ✅ | DS models, incl. bone hierarchy; encode via DAE `--parent` injection |
| ODH | Still image | ✅ | ✅ | GBA-era still image codec |
| PAC | Archive | ✅ | ✅ | Brawl "ARC\0" archive |
| PLT0 | Animation | ✅ | ✅ | Brawl G3D palette-swap animation; IA8, RGB565, RGB5A3 encode via `wimgt` |
| PSDK | Unknown | 🔍 | ⛔ | detected, not decoded |
| QuickLZ | Compression | ✅ | ✅ | both stream versions (1.20, 1.4.0) |
| RARC | Archive | ✅ | ✅ | GameCube / Wii object archive; create via `wszst CREATE .rarc` |
| RL | Compression | ✅ | ✅ | |
| RNC1 | Compression | ✅ | ⛔ | |
| RNC2 | Compression | ✅ | ✅ | encode via `wszst COMPRESS --dest .rnc` |
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

