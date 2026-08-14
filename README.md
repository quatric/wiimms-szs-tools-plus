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

- **External pass-through extraction** for containers with no native
  decoder — Wii/GC disc images, DS ROMs, CIA/3DS, Wii WADs, and Switch
  NSP/XCI/NCA are handed to `wit`/`ndstool`/`ctrtool`/`sharpii`/`hactool`
  and the unpacked tree is recursed into. `--no-passthrough` disables it.
  DS ROMs get one more step: `arm9.bin`/`arm7.bin`/overlay files staged by
  `ndstool` are auto-decompressed in place if they're BLZ-compressed.
- **Recursive directory traversal** for CLI file args via a `**` glob, e.g.
  `wszst DECOMPRESS 'somedir/**/*.ext'`.
- **`wajpg`/`wlzh8`/`wmdlt`** reachable directly from `wimgt`/`wszst`
  without a separate binary (they're still built standalone too).
- **Reliable extraction when recursing into pass-through output** — SARC,
  PAC and GFA archives reached *through* `wit`/`ndstool`/etc. staging
  (rather than passed directly on the command line) used to silently fail
  to extract at all; fixed. GFA's BPE decompression was also fixed against
  real retail data — a full Wii disc's worth of `.gfa` archives now
  extracts end to end instead of 0%.
- **`wbrsar` (BRSAR → MIDI+SF2) actually works now** — it was completely
  non-functional against every real `.brsar` tried, not because of a
  format bug but a linker one: the vgmtrans scanner it depends on
  self-registers via a global-constructor side effect that a plain
  static-library link doesn't preserve, so the whole scanner was being
  silently dropped. Fixed by force-loading the archive in the Makefile.
- **`wszst xx` now recurses into SARC/PAC/GFA extraction output** — GFA
  (and SARC/PAC) extraction used to write its members to disk and stop;
  anything nested inside (a real case: the `.brres` models inside a
  retail Kirby's Epic Yarn disc's `.gfa` archives) never got the rest of
  the XX pipeline, so textures never decoded and no DAE ever came out.
  Two more bugs found closing that loop: the shared DAE exporter had a
  hardcoded "roots only" shortcut discarding all non-root joints for
  every format that uses it (BFRES/BCH/BCRES, not just NSBMD/MDL0), and
  `wszst xx` never actually called the model→DAE exporter for MDL0/NSBMD/
  BFRES/BCH/BCRES files found during extraction (only `wmdlt` did).
- **`wbmsx`'s COMTYPE now covers this fork's own native decoders** —
  `ash0`/`rl`/`huff4`/`huff8`/`huffman`/`rnc`/`rnc1`/`rnc2`/`lzh8`/
  `quicklz`/`qlz`/`blz`/`camelot`/`stpl`, alongside the existing `copy`/
  `lz10`/`lz11`/`yay0`/`zlib`/`deflate`. These aren't stock QuickBMS
  plugin names (quickbms has no Nintendo-specific plugin for most of
  them) — they're this fork's own aliases for decoders already used
  elsewhere, so a BMS script naming compression this way runs natively.

### Format support

| Format | Status |
|---|---|
| AJPG / ODH (GBA-era still image codec) | ✅ |
| BCH (3DS CTR H3D), incl. geometry | ✅ |
| BCFNA / BFFNA (3DS/Wii U font archives) | ⛔ not started — no real samples found anywhere to verify an implementation against |
| BCFNT (3DS bitmap font) / BFFNT (Wii U bitmap font) | 🟡 structure verified on 2 real retail `.bffnt` samples (`wszst xx` → XML: TGLP cell/sheet geometry, sheet count/format id, pointers) — sheet *pixel* decode not done, the format id is a 3DS/Cafe GPU texture format this fork has no table for yet (reusing BRFNT's GX table would silently decode the wrong pixel format). No real `.bcfnt` sample found to verify the 3DS side specifically, but the container/TGLP shape is shared with `.bffnt`. |
| BFLIM / BCLIM textures | ✅ |
| BFLYT / BCLYT / BRLYT + BRLAN / BFLAN / BCLAN (layout) | ✅ |
| BFRES (Wii U) | ✅ |
| BFRES (Switch) | 🟡 structure verified on a real sample (`~/Downloads/Male.bfres`): FMDL/FSHP/FMAT names, vertex-attribute layout (`wszst xx` → XML). Little-endian, version 9+, every offset absolute from file start — reverse engineered field-for-field since it's a completely different, undocumented-in-tree layout from Wii U's. Vertex/index *data* offset convention not resolved — a brute-force scan found a plausible float region but the decode didn't cleanly fall out of the documented fields, so no DAE/geometry yet. |
| BLZ (DS ARM9/ARM7/overlay compression, decode) | ✅ byte-exact vs. the real reference decoder; also auto-applied to `ndstool`-staged executables |
| BNTX (Switch textures) | ✅ (RGBA8/565/5551/4 + BC1-3; BC4-7/ASTC not added) |
| BREFT (Brawl effect texture, palette-indexed) | ✅ decodes with its inline palette |
| BRFNA (Wii font archive) | ⛔ `sheetCount` doesn't mean "contiguous physical sheets" the way BRFNT's does — real gap, found on 2 samples, not guessed around |
| BRFNT (Wii bitmap font) | ✅ verified on 3 diverse retail samples (`wimgt DECODE x.brfnt`) |
| BRRES MDL0 (Wii models) → COLLADA | ✅ materials, per-layer sampler state, UV-set-aware texture binding, skin controllers from bind-pose + NodeMix matrices, cross-archive texture linking — verified on a full retail disc (7864 models, 18193 texture references, zero unresolved). Textures are placed beside the `.dae` by bare filename, since basename-only importers (including macOS Preview/Quick Look) don't follow relative paths that cross directories. |
| BRRES TEX0+PLT0 palette pairing (indexed textures) | ✅ pairs each TEX0 with its PLT0 by reading the name-to-name map in MDL0 material texture-reference records, rather than a naming-convention guess |
| BRSAR (via `wbrsar`) | ✅ produces real MIDI+SF2 from a real retail disc — was completely non-functional (linker was silently dropping the scanner) until this session |
| Camelot TPL / "News Channel" TPL | ✅ |
| CGFX / BCRES (3DS graphics container), incl. geometry | ✅ |
| Compression: LZ10 / LZ11 / RL / Yay0 / ASH0 / LZH8 / QuickLZ | ✅ |
| CTPK (3DS texture container) | ⛔ |
| DS sprites: NCGR / NCLR / NCER / NANR | ✅ |
| GFA / "GFAC" archive | ✅ |
| Mario Party 4-8 `.bin` (`wmpbdump`/`wmpbpack`) | 🟡 synthetic round-trip only, no real disc sample available |
| NSBMD (DS models), incl. bone hierarchy | ✅ verified real skeletal hierarchy on 2 retail samples (was flat/all-root before) |
| PAC (Brawl "ARC\0" archive) | ✅ |
| PLT0 (Brawl G3D palette-swap animation) | ✅ |
| PSDK | 🔍 |
| QuickBMS interpreter (`wbmsx`) | 🟡 `copy`/`lz10`/`lz11`/`yay0`/`zlib`/`deflate` plus this fork's own aliases for its native decoders (`ash0`/`rl`/`huff4`/`huff8`/`huffman`/`rnc`/`lzh8`/`quicklz`/`blz`/`camelot`); anything else still falls back to magic-sniffing or a raw copy |
| RNC1 / RNC2 (Rob Northen Compression) | ✅ |
| WC24 crypto (`wwc24crypt`) | ✅ |

✅ verified · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for what backs each of these.
