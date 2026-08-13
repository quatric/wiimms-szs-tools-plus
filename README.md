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
  decoder — Wii/GC disc images, DS ROMs, CIA/3DS, and Wii WADs are handed
  to `wit`/`ndstool`/`ctrtool`/`sharpii` and the unpacked tree is recursed
  into. `--no-passthrough` disables it.
- **Recursive directory traversal** for CLI file args via a `**` glob, e.g.
  `wszst DECOMPRESS 'somedir/**/*.ext'`.
- **`wajpg`/`wlzh8`/`wmdlt`** reachable directly from `wimgt`/`wszst`
  without a separate binary (they're still built standalone too).
- **`make check-rsa-consumers`** fails the build if anything outside
  `lib-wc24.o` links against the RSA code, keeping it scoped to its one
  legitimate use.

### Format support

| Format | Status |
|---|---|
| AJPG / ODH (GBA-era still image codec) | ✅ |
| BCH (3DS CTR H3D), incl. geometry | ✅ |
| BFLIM / BCLIM textures | ✅ |
| BFLYT / BCLYT / BRLYT + BRLAN / BFLAN / BCLAN (layout) | ✅ |
| BFRES (Wii U) | ✅ |
| BFRES (Switch) | 🔍 |
| BNTX (Switch textures) | ✅ (RGBA8/565/5551/4 + BC1-3; BC4-7/ASTC not added) |
| BRSAR (via `wbrsar`) | 🟡 |
| Camelot TPL / "News Channel" TPL | ✅ |
| CGFX / BCRES (3DS graphics container), incl. geometry | ✅ |
| Compression: LZ10 / LZ11 / RL / Yay0 / ASH0 / LZH8 / QuickLZ | ✅ |
| CTPK (3DS texture container) | ⛔ |
| DS sprites: NCGR / NCLR / NCER / NANR | ✅ |
| GFA / "GFAC" archive | ✅ |
| NSBMD (DS models) | ✅ |
| PAC (Brawl "ARC\0" archive) | ✅ |
| PLT0 (Brawl G3D palette-swap animation) | ✅ |
| PSDK | 🔍 |
| QuickBMS interpreter (`wbmsx`) | 🟡 (no `COMTYPE zlib`) |
| RNC1 / RNC2 (Rob Northen Compression) | ✅ |
| WC24 crypto (`wwc24crypt`) | ✅ |

✅ verified · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for what backs each of these.
