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

| Format | Status |
|---|---|
| AJPG / ODH (GBA-era still image codec) | ✅ |
| ASH0 (compression) | ✅ |
| AT7 (Another Century's Episode / Koei Tecmo archive & compression) | ✅ compression + container extraction |
| BCFNA / BFFNA (3DS/Wii U font archives) | ⛔ no real sample found to verify against |
| BCFNT (3DS bitmap font) / BFFNT (Wii U bitmap font) | 🟡 structure/TGLP only, sheet pixel decode not done |
| BCH (3DS CTR H3D), incl. geometry | ✅ |
| BFLIM / BCLIM textures | ✅ |
| BFLYT / BCLYT / BRLYT + BRLAN / BFLAN / BCLAN (layout) | 🟡 3DS 100% (1980/1980 real files); Wii BRLYT header parsing broken, `txt1` gap on 3DS |
| BFRES (Switch) | 🟡 structure only, no vertex/index data offsets resolved yet |
| BFRES (Wii U) | ✅ |
| BLZ (DS ARM9/ARM7/overlay compression) | ✅ decode only, byte-exact |
| BMS / QuickBMS interpreter (`wbmsx` + `wszst xx --bms`) | ✅ stock + native codec aliases, CLI script chaining |
| BNTX (Switch textures) | 🟡 RGBA8/565/5551/4 + BC1-3; BC4-7/ASTC not added |
| BREFT (Brawl effect texture, palette-indexed) | ✅ |
| BRFNA (Wii font archive, RFNA) | ✅ archived TGLP sheet decompression |
| BRFNT (Wii bitmap font) | ✅ |
| BRRES MDL0 (Wii models) → COLLADA | ✅ materials, skinning, cross-archive textures |
| BRRES TEX0+PLT0 palette pairing | ✅ |
| BRSAR → MIDI+SF2 (`wbrsar`) | ✅ |
| BYML / BYAML (binary YAML) | ✅ decode to YAML, versions 1-4 |
| Camelot TPL / "News Channel" TPL | ✅ |
| CGFX / BCRES (3DS graphics container), incl. geometry | ✅ |
| CTPK (3DS texture container) | ✅ all 14 PICA200 formats |
| DARC (3DS "differential archive" container) | ✅ |
| DS sprites: NCGR / NCLR / NCER / NANR | ✅ |
| GFA / "GFAC" archive | ✅ |
| Huffman 0x24 (4-bit nibble, compression) | ✅ |
| Huffman 0x28 (8-bit byte, compression) | ✅ |
| Mario Party 4-8 `.bin` (MPBIN container) | ✅ |
| NARC (Nitro Archive, DS/3DS container) | ✅ |
| NSBMD (DS models), incl. bone hierarchy | ✅ |
| PAC (Brawl "ARC\0" archive) | ✅ |
| PLT0 (Brawl G3D palette-swap animation) | ✅ |
| PSDK | 🔍 detected, not decoded |
| QuickLZ (compression) | ✅ both stream versions (1.20, 1.4.0) |
| RL (compression) | ✅ |
| RNC1 (compression) | ✅ |
| RNC2 (compression) | ✅ |
| WC24 crypto (`wwc24crypt`) | ✅ |
| Yay0 (compression) | ✅ |
| Zlib / deflate (compression, via BMS) | ✅ |

✅ verified · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for what backs each of these.

