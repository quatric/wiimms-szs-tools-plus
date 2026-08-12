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

This fork (`~/wiimms-szs-tools-nintendo`) adds native (no external binaries)
support for a range of Wii/DS/3DS/Wii U/Switch container, texture, model and
crypto formats beyond what upstream Wiimm's SZS Tools ships. Everything below
is implemented in C in `project/src/`, builds with the normal
`cd project && make all -j4`, and is exercised by `tests/regress.sh`.

**Status legend**

- ✅ **Working / verified** — round-tripped or checked byte-exact against a
  real sample, an independent oracle (e.g. `openssl`, `texture2ddecoder`,
  vendor reference decoders), or a real retail file.
- 🟡 **Partial** — decodes/encodes a meaningful subset; known gaps listed.
- 🔍 **Detection only** — format is recognized (magic/`wszst FILETYPE`) but not
  parsed/decoded.
- ⛔ **Not implemented** — explicitly stubbed, returns failure rather than
  faking success.

This table must stay in sync with the code — update it in the same commit
that changes format support, and don't mark anything ✅ without a real
verification (sample file, oracle, or round-trip), matching the project's
existing "don't ship an unverified guess" norm.

| Format | Status | Notes |
|---|---|---|
| LZ10 / LZ11 / RL / Yay0 / ASH0 / LZH8 / QuickLZ (compression) | ✅ | Round-trip verified for all seven. Compression type is chosen by the **destination file extension** (`wszst COMPRESS in.bin --dest out.lz11`), not a flag. QuickLZ (both 1.20 and 1.4.0 stream versions) is now native, cross-checked against the vendor demo programs (`make qlz-oracle`). |
| GFA / "GFAC" archive (Good-Feel: *Wario Land: Shake It!*, *Kirby's Epic Yarn*) | ✅ | `wszst EXTRACT foo.gfa`. GFCP mode 1 = BPE, modes 2/3 = headerless LZ10. |
| WC24 crypto (`wwc24crypt`) | ✅ | AES-128-OFB + RSA-SHA1 PKCS#1v1.5, confirmed against RiiConnect24/wc24-tools reference source; decrypt output byte-identical to `openssl`. Upstream reference tools themselves disagree on the ciphertext start offset — documented in `wwc24crypt.c`, not "fixed" without evidence. |
| QuickBMS interpreter (`wbmsx`) | 🟡 | Native minimal interpreter (IDSTRING/GET/GOTO/MATH/SET/FOR/IF/COMTYPE copy·lz10·lz11·yay0/ENDIAN/PRINT etc). `COMTYPE zlib` not implemented. |
| BFLYT/BCLYT/BRLYT + BRLAN/BFLAN/BCLAN (layout) | ✅ | `wszst TEXT`/`BINARY`, `wlayt decode\|encode`. A few sub-record types (`prt1`/`txt1`) pad slightly non-canonically (cosmetic). |
| `wszst LAYERS` (layout → composited PNG) | 🟡 | Visual export, not console-accurate: `prt1` sub-layouts aren't instantiated, `wnd1` isn't 9-sliced, texture SRT/blend/vertex-colours ignored. |
| BFLIM/BCLIM textures | ✅ | Formats 0,1,2,3,5,7,8,9,10(ETC1),11(ETC1A4),12,13,20. ETC1 color-block decode is pixel-exact vs `texture2ddecoder` (all modes/flips/tables). Real-corpus result: 689/689. ETC1A4 alpha-nibble order / multi-tile arrangement not independently oracle-checked. |
| BNTX (Switch texture container) | ✅ | Tegra X1 block-linear deswizzle, byte-identical vs reference tooling on 148 cases. Formats: RGBA8/RGB565/RGBA5551/RGBA4 + BC1/BC2/BC3. |
| AJPG / ODH (GBA-era still image codec) | ✅ | `wajpg`, `wimgt DECODE`. Verified on the only two known retail samples (Super Mario Galaxy `allcompleteimage{1,2}.bin`). |
| Camelot TPL / "News Channel" TPL | ✅ | Bounds-checked image-table offset fallback fix in `lib-image2.c`. |
| DS sprites: NCGR/NCLR/NCER/NANR (`wszst SPRITES`) | ✅ | Full OAM decode (all 12 shape/size combos, 8bpp tile-number halving, flips, palette banks). NCGR+NCLR autoguess pairing. Verified on synthetic sets with known expected output. |
| NSBMD (DS models) | ✅ | Full DS display-list decode → DAE. Verified on retail models (`giratina.nsbmd` → 2028 tris, `kawashima.nsbmd` → 4303 tris). Materials/bone hierarchy not resolved. |
| BCH (3DS CTR H3D) | ✅ | Full unrelocated-pointer + relocation-table parse, 14 content dictionaries. Verified: `Mii_body.bch`, `Mii_body_anim.bch`, `SharedData.bch`. |
| BCH geometry | ✅ | Recovered by replaying the PICA200 GPU command list (BCH stores no vertex-layout struct). Verified: `Mii_body.bch` → 6 meshes/340 tris; `Mii_Material*.bch` → exactly 12 tris (cube) each. |
| CGFX / BCRES (3DS graphics container) | ✅ | Self-relative offsets, no relocation needed. Verified on mGBA's 3DS banner (1 model, 6 named textures). |
| CGFX/BCRES geometry | ✅ | Plain interleaved vertex buffer + attribute descriptors (does **not** use PICA200 command lists like BCH). Verified on mGBA's 3DS banner: 6 meshes/1994 tris. |
| BFRES (Wii U, big-endian v3.x) | ✅ | Full FMDL→FVTX/FSHP geometry to DAE, self-relative offsets, GX2 interleaved attribute buffers. Verified on Splatoon's `SPL_box_duck.bfres`: 10 shapes, 1636 tris. |
| BFRES (Switch, little-endian v8+, BNTX-backed) | 🔍 | Detected and explicitly rejected, not parsed. Blocked on samples — no Switch BFRES file has been found to validate against, not blocked on effort. |
| wbrsar (BRSAR → MIDI + SF2, via statically-linked vgmtrans core) | 🟡 | No longer shells out via `system()`. Runs without crashing on garbage input; **not yet verified against a real `.brsar` file** (none found on disk). |
| Recursive directory traversal for CLI file args | ✅ | Not a new flag — this is dclib's existing shell-glob-style `**` wildcard (`SearchPaths()`/`search_paths_dir()` in `dclib/dclib-file.c`), already wired into every tool's argument expansion (`CollectExpandParam`). `wszst DECOMPRESS 'somedir/**/*.ext'` (or `wimgt DECODE`, etc.) recursively walks and processes every matching file under `somedir`; a bare directory with no `**` is unaffected (fails to open, as before). Verified in `tests/regress.sh`. |

**Known stubs / explicitly unimplemented** (return failure rather than faking
success, by design):

- `ParseBFRES`/`ParseNSBMD` fallback paths return `NULL` on unrecognized
  sub-variants rather than a fake empty model.
- RSA beyond WC24's sign/verify use case.
- QuickBMS `COMTYPE zlib`.
- Switch BFRES parsing (see table above).

The table above is the current summary of that verification detail: what was
checked against, which real samples were used, and what remains unverified.
