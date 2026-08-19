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
- Validated the existing **Monster Games RST/TOC + QuickLZ** container
  support (used by Excite Truck, ExciteBots: Trick Racing, NASCAR Heat)
  against a real retail disc: all 36 real `.car`/`.toc` pairs from
  ExciteBots: Trick Racing (USA) extracted cleanly with `wszst EXTRACT`,
  no errors, entry counts ranging 22-54 per archive. No decode bugs found;
  this was a validation pass, not a fix.

See the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for the full history of what was fixed, how each format was verified, and
against which real samples — not duplicated here.

### Format & compression support

| Format | Category | Decode | Encode | Notes |
|---|---|---|---|---|
| AJJPG / AJPG | Still image | ✅ | ✅ | GBA-era still image container |
| ASH0 | Compression | ✅ | ✅ | |
| AT7 | Archive/compression | ✅ | ✅ | Another Century's Episode / Koei Tecmo |
| BCFNT | Font | 🟡 | ✅ | 3DS bitmap font; structure/TGLP decode, encode via `wimgt` |
| BCH | Model | ✅ | ✅ | 3DS CTR H3D, incl. geometry; encode via DAE `--parent` injection |
| BCLAN | Layout | ✅ | ✅ | 3DS layout animation; shares BCLYT's parser/encoder, same status |
| BCLIM | Texture | ✅ | ✅ | 3DS textures |
| BCLYT | Layout | ✅ | ✅ | 3DS layout; 1980/1980 real files decode AND byte-exact round-trip (decode→encode→decode) against a real cartridge dump |
| BCRES | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| BCSAR | Audio archive | ✅ | ✅ | 3DS Sound Archive (CSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`) |
| BCWAV | Audio track | ✅ | ⛔ | 3DS Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV |
| BCWAR | Audio archive | ✅ | ✅ | 3DS Sound Wave Archive (CWAR); unpacks member BCWAV audio tracks and repacks (`wszst CREATE`) |
| BCGRP | Audio archive | ✅ | ✅ | 3DS Sound Group Archive (CGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
| BFFNT | Font | 🟡 | ✅ | Wii U bitmap font; structure/TGLP decode, encode via `wimgt` |
| BFLAN | Layout | 🟡 | 🟡 | Wii U layout animation; shares BCLYT's parser/encoder for its own sections — not independently checked for the BFLYT-vs-BCLYT struct divergence found 2026-08-15 |
| BFLIM | Texture | ✅ | ✅ | Wii U textures, incl. BC1/BC2/BC3/BC4/BC5 block-compressed formats (fmt 14-17, 21-23) |
| BFLYT | Layout | 🟡 | 🟡 | Wii U layout; does NOT share BCLYT's struct layout (correction — see below); pan1/lyt1/grp1/mat1/prt1/txt1 fixed for real Wii U files, 506/561 (90%) real files fully parse (decode only — encoders still 3DS-shaped); cnt1 remains unexamined |
| BFRES | Model | 🟢 | ⛔ | Switch; geometry decode (position/normal/UV, first LOD mesh) to DAE verified against real Super Mario Odyssey retail data (v8+v9); falls back to the names/shapes/materials-only structure XML for the rare shape it can't decode yet |
| BFRES | Model | ✅ | ✅ | Wii U; encode via DAE `--parent` injection; FMAT materials bound to their first FTEX texture ref, decoded+PNG'd during extraction (98.8% of a real disc's models resolve a diffuse texture) |
| BFSAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Archive (FSAR); recursive member & wave archive extraction (`wszst xx`) and creation (`wszst CREATE`) |
| BFWAV | Audio track | ✅ | ⛔ | Wii U / Switch Sound Wave; DSP-ADPCM, IMA-ADPCM, PCM16, PCM8 decoding to WAV |
| BFWAR | Audio archive | ✅ | ✅ | Wii U / Switch Sound Wave Archive (FWAR); unpacks member BFWAV audio tracks and repacks (`wszst CREATE`) |
| BFGRP | Audio archive | ✅ | ✅ | Wii U / Switch Sound Group Archive (FGRP); unpacks embedded audio files and repacks (`wszst CREATE`) |
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
| BRRES TEX0 | Texture | ✅ | ⛔ | Wii textures; palette pairing w/ PLT0 |
| BRSAR | Audio | ✅ | ⛔ | → MIDI+SF2 (`wbrsar`) |
| BYAML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| BYML | Data | ✅ | ✅ | binary YAML; encode via `wszst CREATE .byml` |
| CCF | Archive | ✅ | ✅ | Wii/Switch Virtual Console archive, optional zlib compression; create via `wszst CREATE .ccf` |
| CGFX | Model | ✅ | ✅ | 3DS graphics container, incl. geometry; encode via DAE `--parent` injection |
| CTPK | Texture / Archive | ✅ | ✅ | 3DS multi-texture container; encode via `wimgt` or folder create via `wszst CREATE .ctpk` |
| DARC | Archive | ✅ | ✅ | 3DS "differential archive" container |
| Deflate | Compression | ✅ | ✅ | via BMS & wszst; encode via `wszst COMPRESS --dest .deflate` |
| GFA | Archive | ✅ | ✅ | "GFAC" archive; create via `wszst CREATE .gfa` |
| GTX / GSH | Texture | 🟡 | ⛔ | Wii U GX2 texture container ("Gfx2"); RGBA8/R8/R8G8/565/5551/4444 + BC1-5 decode, tile modes 1/2/3/4/7/8/11 (aspect-1, non-bank-swapped); bank-swapped/other-aspect modes and shader (.gsh) blocks not decoded |
| Huffman 0x24 | Compression | ✅ | ✅ | 4-bit nibble |
| Huffman 0x28 | Compression | ✅ | ✅ | 8-bit byte |
| Mario Party `.bin` | Archive | ✅ | ✅ | MPBIN container, games 4-8; unpacks & repacks via `wszst CREATE .bin` |
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

