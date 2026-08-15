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

See the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for the full history of what was fixed, how each format was verified, and
against which real samples — not duplicated here.

### Format & compression support

| Format | Decode | Encode | Notes |
|---|---|---|---|
| AJPG / ODH (GBA-era still image codec) | ✅ | ✅ | |
| ASH0 (compression) | ✅ | ✅ | |
| AT7 (Another Century's Episode / Koei Tecmo archive & compression) | ✅ | ✅ | |
| BCFNT (3DS bitmap font) / BFFNT (Wii U bitmap font) | 🟡 | ⛔ | structure/TGLP only |
| BCH (3DS CTR H3D), incl. geometry | ✅ | ✅ | encode via DAE `--parent` injection |
| BFLIM / BCLIM textures | ✅ | ✅ | |
| BFLYT (3DS layout) | 🟡 | 🟡 | 1980/1980 real files parse; known `txt1` field gap |
| BCLYT (Wii U layout) | 🟡 | 🟡 | shares BFLYT's parser/encoder, same status |
| BFLAN (3DS layout animation) | 🟡 | 🟡 | shares BFLYT's parser/encoder, same status |
| BCLAN (Wii U layout animation) | 🟡 | 🟡 | shares BFLYT's parser/encoder, same status |
| BRLYT (Wii layout) | ⛔ | ⛔ | header parsing broken |
| BRLAN (Wii layout animation) | ✅ | ✅ | lossless text roundtrip via wlayt |
| BFRES (Switch) | 🟡 | ⛔ | structure only |
| BFRES (Wii U) | ✅ | ✅ | encode via DAE `--parent` injection |
| BLZ (DS ARM9/ARM7/overlay compression) | ✅ | ✅ | |
| BMS / QuickBMS interpreter (`wbmsx` + `wszst xx --bms`) | ✅ | 🟡 | native codec aliases only |
| BNTX (Switch textures) | 🟡 | ✅ | RGBA8/565/5551/4 + BC1-3 decode; RGBA8 encode |
| BREFT (Brawl effect texture, palette-indexed) | ✅ | ⛔ | |
| BRFNA (Wii font archive, RFNA) | ✅ | ⛔ | |
| BRFNT (Wii bitmap font) | ✅ | ⛔ | |
| BRRES MDL0 (Wii models) → COLLADA | ✅ | ✅ | encode via DAE `--parent` injection |
| BRRES TEX0+PLT0 palette pairing | ✅ | ⛔ | |
| BRSAR → MIDI+SF2 (`wbrsar`) | ✅ | ⛔ | |
| BYML / BYAML (binary YAML) | ✅ | ⛔ | versions 1-4 |
| Camelot TPL / "News Channel" TPL | ✅ | ✅ | encode via `wszst COMPRESS --stpl` |
| CGFX / BCRES (3DS graphics container), incl. geometry | ✅ | ✅ | encode via DAE `--parent` injection |
| CTPK (3DS texture container) | ✅ | ✅ | |
| DARC (3DS "differential archive" container) | ✅ | ✅ | |
| DS sprites: NCGR / NCLR / NCER / NANR | ✅ | 🟡 | NCGR/NCLR encode via `wimgt` |
| GFA / "GFAC" archive | ✅ | ✅ | create via `wszst CREATE .gfa` |
| Huffman 0x24 (4-bit nibble, compression) | ✅ | ✅ | |
| Huffman 0x28 (8-bit byte, compression) | ✅ | ✅ | |
| Mario Party 4-8 `.bin` (MPBIN container) | ✅ | ✅ | |
| NARC (Nitro Archive, DS/3DS container) | ✅ | ✅ | |
| NSBMD (DS models), incl. bone hierarchy | ✅ | ✅ | encode via DAE `--parent` injection |
| PAC (Brawl "ARC\0" archive) | ✅ | ✅ | |
| PLT0 (Brawl G3D palette-swap animation) | ✅ | ⛔ | |
| PSDK | 🔍 | ⛔ | detected, not decoded |
| QuickLZ (compression) | ✅ | ✅ | both stream versions (1.20, 1.4.0) |
| RL (compression) | ✅ | ✅ | |
| RNC1 (compression) | ✅ | ⛔ | |
| RNC2 (compression) | ✅ | ⛔ | |
| WC24 crypto (`wwc24crypt`) | ✅ | ✅ | |
| WUD / WUX (Wii U disc image) | ✅ | ⛔ | pass-through via `wud2app`+`cdecrypt`, native WUX decompress |
| Yay0 (compression) | ✅ | ✅ | |
| Zlib / deflate (compression, via BMS) | ✅ | ⛔ | |

✅ supported · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for verification details and real samples used for each of these.

