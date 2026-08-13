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
| RNC1 / RNC2 (Rob Northen Compression) | ✅ | Ported from the decompiled RNC ProPack source (`lab313ru/rnc_propack_source`): 18-byte header + CRC check, m1 Huffman/LZ77 and m2 match/raw decode, 0xFFFF window with flush. Detected in `DetectNintendoFormat` (methods 1-3) and wired into `decompress_nintendo_file`. Keyed streams are rejected rather than mishandled. |
| PSDK | 🔍 | Recognized by `DetectNintendoFormat` and reported as such, but not decoded. |
| WC24 crypto (`wwc24crypt`) | ✅ | AES-128-OFB + RSA-SHA1 PKCS#1v1.5, confirmed against RiiConnect24/wc24-tools reference source; decrypt output byte-identical to `openssl`. Upstream reference tools themselves disagree on the ciphertext start offset — documented in `wwc24crypt.c`, not "fixed" without evidence. |
| QuickBMS interpreter (`wbmsx`) | 🟡 | Native minimal interpreter (IDSTRING/GET/GOTO/MATH/SET/FOR/IF/COMTYPE copy·lz10·lz11·yay0/ENDIAN/PRINT etc). `COMTYPE zlib` not implemented. |
| BFLYT/BCLYT/BRLYT + BRLAN/BFLAN/BCLAN (layout) | ✅ | `wszst TEXT`/`BINARY`, `wlayt decode\|encode`. A few sub-record types (`prt1`/`txt1`) pad slightly non-canonically (cosmetic). |
| BFLIM/BCLIM textures | ✅ | Formats 0,1,2,3,5,7,8,9,10(ETC1),11(ETC1A4),12,13,20. ETC1 color-block decode is pixel-exact vs `texture2ddecoder` (all modes/flips/tables). Real-corpus result: 689/689. ETC1A4 alpha-nibble order / multi-tile arrangement not independently oracle-checked. |
| BNTX (Switch texture container) | ✅ | Tegra X1 block-linear deswizzle, byte-identical vs reference tooling on 148 cases. Formats: RGBA8/RGB565/RGBA5551/RGBA4 + BC1/BC2/BC3. BC4/BC5/BC6H/BC7/ASTC/sRGB byte codes not added — no reachable source for their `SurfaceFormat` values and no real `.bntx` sample to verify against; left unimplemented rather than guessed. CTPK (3DS texture container) also unimplemented, no sample/oracle. |
| PLT0 (Brawl G3D palette-swap animation) | ✅ | `wszst DECODE`/`wimgt DECODE` on `.plt0`. Field offsets matched BrawlLib's source from the start, but the decoder tagged output with the wrong internal image format, so every real file decoded wrong — found and fixed against 3 real retail `.plt0` files, verified pixel-for-pixel against hand-decoded raw bytes. |
| PAC (Brawl "ARC\0" archive) | ✅ | `wszst EXTRACT foo.pac`. New parser from BrawlLib's `ARC.cs` source. Verified against 4 real retail `.pac` files: parsed structure accounts for the file size exactly, zero slack. Not yet cross-checked against a `.pac` with an embedded PLT0 (Brawl typically stores palettes in BRRES instead). |
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
| External pass-through extraction (`wszst XX`/`EXTRACT`/`XCOMMON`/`XALL`) for containers with no native decoder | ✅ | Wii/GC disc images (WBFS/WDF/CISO/raw ISO, by header signature) → `wit`; Nintendo DS ROMs (`NINTENDO` header tag, or `.nds` extension) → `ndstool`; `.cia`/`.3ds`/`.cci`/`.cxi` → `ctrtool --plaintext`; Wii WAD (header-verified, `.wad`/`.app`) → `sharpii`. The external tool unpacks into a `<source>.d` staging directory beside the source (matching every native extractor's own `\1P/\1N.d` convention), then the staged tree is walked recursively so every leaf gets the normal native-decoder treatment. Disabled with `--no-passthrough`; tool path/name overridden per-tool with `--with-wit`/`--with-ndstool`/`--with-ctrtool`/`--with-sharpii`. Verified end-to-end against real files: a retail `.nds` (arm9/arm7/filesystem staged via `ndstool`) and a retail Wii Channel `.wad` (12 content `.app` files staged via `sharpii`, `tests/regress.sh`). Two real bugs were found and fixed during verification: `ndstool`'s `-y` flag is an *overlay-files directory*, not a "write rominfo.xml" flag (an earlier draft invented that non-existent flag and got an empty directory named `rominfo.xml` back); and a WAD's own unpacked `.app` content files (raw ELF/binary payloads) were being extension-matched and re-submitted to `sharpii` as nested WADs, which reliably failed — fixed by requiring the real WAD/boot2 header signature (`00 00 00 20`/`00 00 00 40` + `Is\0\0`/`ib\0\0`) before an `.app`/`.wad` extension claim is honoured. `ctrtool` untested (not installed on this machine). |

**Fork-added command-line tools**

Standalone binaries this fork adds alongside upstream's `wszst`/`wimgt`/etc.
Each is a normal `make all` build target (see `MAIN_TOOLS`/`TEST_TOOLS` in
`project/Makefile`). Some of the same functionality is also reachable as a
`wszst` subcommand where noted; some is standalone-only for now (see the note
at the end of this section for why).

| Tool | Usage | Notes |
|---|---|---|
| `wajpg` | `wajpg encode <in.ppm> <out.ajpg> [quality]` / `wajpg decode <in.ajpg> <out.ppm>` / `wajpg info <in.ajpg>` | AJPG/ODH codec. **Folded into `wimgt`**: `wimgt ENCODE in.png -d out.ajpg` and `wimgt DECODE in.ajpg -d out.png` dispatch on the `.ajpg` extension through the same `SaveAJPG`/`LoadAJPG` backend — verified round-trip via the standalone binary (lossy DCT, max per-byte deviation 22/255 on a real retail sample — see AJPG row above) and re-verified through `wimgt` directly. |
| `wlzh8` | `wlzh8 cmp\|dec [options...]` | Nintendo LZH8 compress/decompress. **Folded into `wszst`**: `wszst wlzh8 cmp\|dec ...` dispatches to the same code via `wszst`'s multi-call wrapper table (`wrapper_tab[]` in `wszst.c`) — round-trip re-verified through that path. |
| `wbmsx` | `wbmsx <script.bms> <input_file> <output_dir>` | QuickBMS-style script interpreter. Also reachable as `wszst BMS`. |
| `wwc24crypt` | `wwc24crypt wc24-decrypt\|wc24-encrypt ...` | WC24 AES-128-OFB + RSA-SHA1 crypto. Also reachable as `wszst WC24DECRYPT`/`WC24ENCRYPT`. |
| `wlayt` | `wlayt decode <input> [output]` / `wlayt encode <input> [output]` | BRLYT/BFLYT/BCLYT + BRLAN/BFLAN/BCLAN <-> text. Also reachable as `wszst TEXT`/`BINARY`. |
| `wmdlt` | `wmdlt <command> [options] [files]` | MDL/NSBMD/BCH/CGFX/BFRES model tool — decode to DAE, encode text MDL. **Folded into `wszst`**: `wszst wmdlt <command> ...` dispatches through the same `wrapper_tab[]` mechanism (this was already wired, unrelated to the AJPG/LZH8 fix below). |
| `wbrsar` | `wbrsar <input.brsar> <output_dir>` | BRSAR (or other vgmtrans-recognized bank) → MIDI + SF2, via the statically-linked vgmtrans core (no subprocess). Not yet verified against a real `.brsar` file — see table above. |

`wajpg`/`wlzh8`/`wmdlt` are all still built as their own standalone binaries
(same code, `MAIN_TOOLS`/`WRAPPER_TOOLS` in `project/Makefile`), but their
functionality is now also reachable from `wimgt`/`wszst` directly without a
separate binary. This is a **different, working mechanism** from the one
`wszst AJPG`/`wszst LZH8`/`wszst MDL` subcommands would have used: an earlier,
unfinished pass declared those three in the UI-definition layer
(`ui.def`/`tab-wszst.inc`) but never wired actual command dispatch in
`wszst.c`, and that unwired UI declaration was reverted rather than shipped
(see the memory file for the full story). The extension-based `wimgt`
dispatch and the `wrapper_tab[]`-based `wszst wlzh8`/`wszst wmdlt` dispatch
used here are separate, already-existing mechanisms that needed no UI-def
changes — `wlzh8`'s `main()` was already renamed to `main_wlzh8` and linked
into `wszst` in an earlier pass, it just wasn't registered in `wrapper_tab[]`
yet; that one-line registration is what this pass added.

**Known stubs / explicitly unimplemented** (return failure rather than faking
success, by design):

- `ParseBFRES`/`ParseNSBMD` fallback paths return `NULL` on unrecognized
  sub-variants rather than a fake empty model.
- RSA beyond WC24's sign/verify use case.
- QuickBMS `COMTYPE zlib`.
- Switch BFRES parsing (see table above).

The table above is the current summary of that verification detail: what was
checked against, which real samples were used, and what remains unverified.
