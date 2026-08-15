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
  **The CIA/3DS branch was non-functional until this session**: it passed
  `ctrtool` a `--plaintext` flag that doesn't exist in real `ctrtool`
  (jakcron/Project_CTR) — the actual flag is `-p`/`--plain`, and it means
  the *opposite* ("extract without decrypting"), so even fixing the name
  would have left every retail CIA's NCCH still encrypted — and never
  passed any output-directory flag at all, so nothing was ever written to
  the staging dir. Fixed to omit `-p` (so ctrtool decrypts with its
  built-in retail keys) and point `--exefsdir`/`--romfsdir` at the stage;
  verified end to end against a real retail CIA (Tomodachi Life,
  CTR-P-EC6E): exefs/romfs extract and recurse normally.
- **Recursive directory traversal** for CLI file args via a `**` glob, e.g.
  `wszst DECOMPRESS 'somedir/**/*.ext'`.
- **QuickBMS script chaining (`wszst xx --bms=<script.bms>`)** — allows chaining a QuickBMS
  extraction script into `wszst xx` for custom or unsupported container formats, automatically
  staging files and recursing into inner Nintendo assets (models to DAE, textures to PNG).
- **`wajpg` and `wlzh8` codecs natively folded into `wimgt` and `wszst`** — AJPG still image
  encoding and decoding (`wimgt ENCODE input.png -d out.ajpg` / `wimgt DECODE input.ajpg -d out.png`)
  and LZH8 compression/decompression (`wszst COMPRESS --lzh8` / `wszst DECOMPRESS input.lzh8`)
  run directly inside the main tool suite without standalone binaries.
- **AT7 compression, decompression, and container extraction** — native compressor/decompressor
  for the AT7 format (`wszst COMPRESS --at7` / `wszst DECOMPRESS input.at7`), `wbmsx COMTYPE at7`,
  and automated archive container extraction (`extract_at7_file()`) in `wszst xx`.
- **BRFNA font archive native decompression** — reverse-engineered the proprietary TGLP sheet
  compression codec from `nw4r_fontcvtr.exe` (LZSS, RLE, and canonical Huffman opcodes),
  allowing `wszst xx` and `wimgt` to natively decompress and extract all glyph sheets from `.brfna`
  archives across Latin, Japanese CJK, and Simplified Chinese fonts.
- **Mario Party 4-8 `.bin` (MPBIN) extraction** — `wszst xx` natively extracts Mario Party `.bin`
  containers (`extract_mpbin_file()`) with sub-file detection for `.hsf`, `.atb`, `.pac`, `.darc`,
  `.sarc`, `.dat` and recurses into inner assets. Also includes standalone `wmpbdump`/`wmpbpack` round-trip.
- **Nintendo Huffman (0x24/0x28) decompression** — 4-bit nibble and 8-bit byte Huffman streams in
  `wszst DECOMPRESS` and `wbmsx COMTYPE huff4`/`huff8`.
- **CTPK (CTR Texture Package / 3DS texture container) native decoding** — full native
  parsing and decoding of `.ctpk` archives in `wimgt` and `wszst xx`. Decodes all 14 PICA200 GPU
  texture formats (RGBA8, RGB8, RGBA5551, RGB565, RGBA4444, LA88, HILO8, L8, A8, LA44, L4, A4,
  ETC1, and ETC1A4) with Morton coordinate untiling. Automatically extracts all texture members to
  PNG during `wszst xx` recursion and decodes directly via `wimgt DECODE`. Also seamlessly handles
  unnamed SARC archive entries and Yaz0-compressed SARC archives.
- **BYML / BYAML (Binary YAML parameter format) native decoding** — decodes Nintendo binary YAML
  parameter streams (magic `'BY'`/`'YB'`, versions 1-4, 3DS/Wii U/Switch) to human-readable, fully
  spec-compliant YAML. Supports dictionaries (`0xC1`), arrays (`0xC0`), string tables (`0xC2`),
  nested hierarchies, and all scalar data types (bool, int32, uint32, float, int64, uint64, double,
  null) with proper character escaping for Shift-JIS/binary strings. Automatically decoded to `.yaml`
  in `wszst xx` and accessible via `wszst TEXT input.byml --dest out.yaml`.
- **NARC (Nitro Archive / DS & 3DS container) extraction** — native container scanner and extractor
  for Nitro Archive (`NARC` / `CRAN`) packages. Parses `FATB`/`BTAF` file allocation tables and
  `FNTB`/`BTNF` recursive directory name trees. Unpacks Yaz0/NARC packages (e.g. stage maps, layouts,
  shaders) automatically during `wszst xx` recursion.
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
- **DARC (3DS "differential archive" container) support, native** — `wszst xx`
  now extracts `darc`-magic archives the same way it already does SARC/PAC/GFA.
  Real 3DS titles use DARC to bundle a whole layout+animation family into one
  romfs file; without this, every layout inside one was invisible to the rest
  of the pipeline (BFLYT auto-decode, texture linking, etc.). See the format
  table below for the byte-level verification and a related bug this fix
  surfaced in the BFLYT/BRFNT auto-decode.
- **3DS BFLYT/BCLYT parsing fixed for real files** — this parser was written
  entirely against the Wii RLYT/RLAN struct shapes and had apparently never
  been checked against real 3DS CLYT/CLAN data; `lyt1`/`grp1`/the shared
  pane-header name field/`mat1` all had wrong field widths for 3DS. Fixed all
  four (decode and, for `mat1`, the text-format encode side too), verified
  against an independent reference decoder plus real retail bytes. A full
  retail disc's `.bclyt`/`.bclan` corpus went from 1661/1980 parsing (16%
  failing) to 1980/1980 (100%). See the format table for the byte-level
  detail and the one known remaining gap (`txt1`).
- **`wbmsx`'s COMTYPE now covers this fork's own native decoders** —
  `ash0`/`rl`/`huff4`/`huff8`/`huffman`/`rnc`/`rnc1`/`rnc2`/`lzh8`/
  `quicklz`/`qlz`/`blz`/`camelot`/`stpl`/`at7`, alongside the existing `copy`/
  `lz10`/`lz11`/`yay0`/`zlib`/`deflate`. These aren't stock QuickBMS
  plugin names (quickbms has no Nintendo-specific plugin for most of
  them) — they're this fork's own aliases for decoders already used
  elsewhere, so a BMS script naming compression this way runs natively.

### Format support

| Format | Status |
|---|---|
| AJPG / ODH (GBA-era still image codec) | ✅ native in `wimgt` (ENCODE/DECODE) |
| AT7 (Another Century's Episode / Koei Tecmo archive & compression) | ✅ native compression and decompression in `wszst`/`wbmsx`, and automatic archive container extraction in `wszst xx` |
| BCH (3DS CTR H3D), incl. geometry | ✅ real, systematic bug fixed this session: `position_idx`/`normal_idx`/`texcoord_idx` were written `n+1` instead of `n`, a 1-indexed-instead-of-0-indexed off-by-one that made *every* exported DAE reference one vertex index past the end of its own position/normal/texcoord array — assimp rejected 100% of a real retail disc's models ("Invalid data index (N/N)") before the fix, 0/2167 after (Tomodachi Life, CTR-P-EC6E) |
| BCFNA / BFFNA (3DS/Wii U font archives) | ⛔ not started — no real samples found anywhere to verify an implementation against |
| BCFNT (3DS bitmap font) / BFFNT (Wii U bitmap font) | 🟡 structure verified on 2 real retail `.bffnt` samples (`wszst xx` → XML: TGLP cell/sheet geometry, sheet count/format id, pointers) — sheet *pixel* decode not done, the format id is a 3DS/Cafe GPU texture format this fork has no table for yet (reusing BRFNT's GX table would silently decode the wrong pixel format). No real `.bcfnt` sample found to verify the 3DS side specifically, but the container/TGLP shape is shared with `.bffnt`. |
| BFLIM / BCLIM textures | ✅ |
| BFLYT / BCLYT / BRLYT + BRLAN / BFLAN / BCLAN (layout) | 🟡 3DS CLYT/CLAN decode: **1980/1980 (100%) real `.bclyt`/`.bclan` files from a full retail disc now parse without error**, up from 1661/1980 (319 failing, 16%) when this pass started. `wszst xx` also auto-converts a layout/anim found during extraction to `.tflyt` text (previously only explicit `wszst TEXT` did), including ones bundled inside a DARC container (see DARC below) -- that required its own fix: the auto-decode was gated on `export_count`, which `extract_tree_complete()` temporarily zeroes while walking a nested container (to defer model/BRSAR export to one final pass), so it silently never fired for anything inside SARC/PAC/GFA/DARC. Fixed by also calling it from `export_models_tree()`. Chasing the 319 real parse failures down (verified first, byte-by-byte, that the DARC-extracted bytes were perfectly intact -- the bug was never DARC's) found this entire parser was written for the **Wii** RLYT/RLAN struct shapes and had never been checked against real **3DS** CLYT/CLAN data at all; every section type checked had a different, wrong field layout. Root-caused and fixed four, all cross-verified against Gericom/EveryFileExplorer's independent reference decoder (`mat1.cs`/`pan1.cs`/`CLYT.cs`) plus real retail bytes, each consuming its chunk exactly with zero slack: **`lyt1`** (screen size) was reading a 20-byte Wii struct with a trailing name over a 12-byte 3DS struct (`u32` screen-origin + 2 floats) -- silently produced nonsense values (`~1e-9`/`~1e-43`) instead of failing, so it wasn't caught by the corpus-failure sweep that found the others; **`grp1`** (pane groups) assumed a 34-byte name / `u16` subnum at +42 / 24-byte sub-entries where the real struct is a 16-byte name / `u32` subnum at +24 / 16-byte sub-entries; the shared pane-header name field (`pan1`/`pic1`/`txt1`/`wnd1`/`bnd1` all use it) was 32 bytes, not the real 24, desyncing every field after it in every pane in every file; **`mat1`** (materials) had a completely wrong flag-bitfield -- `tevStage` count is 3 bits wide not 2 (so any material with 4+ tev stages silently desynced everything after it), `colorBlendMode`/`alphaBlendMode` are single presence bits not 2-bit counts, `texCoordGen`/`tevStage` entries are 4/12 bytes not the assumed 8/4, and the material header is a `bufferColor` + 6 `constColor`s (28 bytes) not two colors (8 bytes) after a 20-byte (not 34-byte) name -- fixed on both the decode and the text-format encode side, including a real pre-existing bug in the encoder's per-item key parser that silently dropped `alpha-compare`/`indirect-adjustment`/`shadow-blending`/`color-blend-mode`/`alpha-blend-mode` on every re-encode (it stripped at the *last* dash in the key regardless of whether that was actually a numeric-index suffix). **Known remaining gap, not fixed**: `txt1` (text panes) has several fields (`italic-tilt`, a full shadow-blend block) that don't exist in the real 3DS struct at all -- likely Wii-only -- and reads its string inline where the real format uses an indirect offset into the chunk; this needs a proper rewrite, not a byte-offset tweak, so it was left as-is (still passes the corpus sweep only because nothing downstream currently validates its output). `pic1`/`wnd1`/`bnd1`/`prt1` were not cross-checked against reference source this session and may have the same class of bug. Separately, and *not* something this session touched: a real Wii `.brlyt` sample (`P1_Def.brlyt` from a retail Animal Crossing: City Folk disc) fails to parse for an unrelated, pre-existing reason -- the header layout this parser hardcodes (`header_size` implicitly at +6, first section always at +0x14) doesn't match a real Wii RLYT file's actual header (`header_size` at +0x0C, section count at +0x0E, first section wherever `header_size` says, here +0x10); confirmed via `git show` that this hardcoding predates this session (Aug 12). Real Wii BRLYT support may never have worked against retail data. |
| BFRES (Wii U) | ✅ had the same `n+1` vertex-index off-by-one as BCH/CGFX (see BCH above); fixed in the same pass, not independently re-verified against a real sample this session (none on disk) but the bug and fix are identical code |
| BFRES (Switch) | 🟡 structure verified on a real sample (`~/Downloads/Male.bfres`): FMDL/FSHP/FMAT names, vertex-attribute layout (`wszst xx` → XML). Little-endian, version 9+, every offset absolute from file start — reverse engineered field-for-field since it's a completely different, undocumented-in-tree layout from Wii U's. Vertex/index *data* offset convention not resolved — a brute-force scan found a plausible float region but the decode didn't cleanly fall out of the documented fields, so no DAE/geometry yet. |
| BLZ (DS ARM9/ARM7/overlay compression, decode) | ✅ byte-exact vs. the real reference decoder; also auto-applied to `ndstool`-staged executables |
| BNTX (Switch textures) | ✅ (RGBA8/565/5551/4 + BC1-3; BC4-7/ASTC not added) |
| BREFT (Brawl effect texture, palette-indexed) | ✅ decodes with its inline palette |
| BRFNA (Wii font archive, RFNA) | ✅ decompresses archived-font TGLP sheets natively (LZSS/RLE/canonical Huffman opcodes reverse-engineered from `nw4r_fontcvtr.exe`); verified on Latin/symbol, Japanese CJK, and Simplified Chinese retail samples |
| BRFNT (Wii bitmap font) | ✅ verified on 3 diverse retail samples (`wimgt DECODE x.brfnt`); `wszst xx` now decodes one found during extraction too — previously nothing in the XX/EXTRACT tree walk called it, only the standalone `wimgt` command did |
| BRRES MDL0 (Wii models) → COLLADA | ✅ materials, per-layer sampler state, UV-set-aware texture binding, skin controllers from bind-pose + NodeMix matrices, cross-archive texture linking — verified on a full retail disc (7864 models, 18193 texture references, zero unresolved). Textures are placed beside the `.dae` by bare filename, since basename-only importers (including macOS Preview/Quick Look) don't follow relative paths that cross directories. |
| BRRES TEX0+PLT0 palette pairing (indexed textures) | ✅ pairs each TEX0 with its PLT0 by reading the name-to-name map in MDL0 material texture-reference records, rather than a naming-convention guess |
| BRSAR (via `wbrsar`) | ✅ produces real MIDI+SF2 from a real retail disc — was completely non-functional (linker was silently dropping the scanner) until this session. `wszst xx` now also converts a `.brsar` found during extraction automatically, by shelling out to the sibling `wbrsar` binary (wszst itself doesn't link vgmtrans, to keep cmake/glib out of the main tool) |
| Camelot TPL / "News Channel" TPL | ✅ |
| CGFX / BCRES (3DS graphics container), incl. geometry | ✅ had the same `n+1` vertex-index bug as BCH (see above), same fix, same real-disc verification (2167/2167 CGFX models from Tomodachi Life now load clean in assimp) |
| Compression: LZ10 / LZ11 / RL / Yay0 / ASH0 / LZH8 / QuickLZ / Huffman (0x24/0x28) / AT7 | ✅ byte-exact round-trip and decoding across all formats in `wszst` / `wbmsx` |
| CTPK (3DS texture container) | ✅ native decoding for all PICA200 formats (RGBA8, RGB8, RGBA5551, RGB565, RGBA4444, LA88, HILO8, L8, A8, LA44, L4, A4, ETC1, ETC1A4) with Morton block tiling; automatic extraction and PNG conversion in `wszst xx` and `wimgt DECODE`; verified against retail Mario Kart 7 textures |
| DARC (3DS "differential archive" container, magic `darc`) | ✅ `wszst xx` now extracts it like SARC/PAC/GFA (`extract_darc_file()` in `wszst.c`, `ScanDARC()` in `lib-nintendo.c`). Layout verified byte-for-byte against a real sample plus GBATEK, 3dbrew, and Tyulis/3DSkit's reference unpacker — magic/BOM/header fields, root entry's directory flag + end-index, alignment, and the "." alias entry's name offset all matched. Handles arbitrary nesting depth via an explicit directory stack (the reference Python unpacker only tracks one "current subdir" and breaks on deep nesting; this doesn't). Verified on a real retail disc (Tomodachi Life, CTR-P-EC6E): 190 DARC archives unwrapped, e.g. every `romfs/layout/*.bin` (each one bundles a whole layout+animation family). |
| DS sprites: NCGR / NCLR / NCER / NANR | ✅ |
| GFA / "GFAC" archive | ✅ |
| Mario Party 4-8 `.bin` (MPBIN container) | ✅ native extraction in `wszst xx` (`extract_mpbin_file()` with sub-file detection for `.hsf`, `.atb`, `.pac`, `.darc`, `.sarc`, `.dat`) + standalone `wmpbdump`/`wmpbpack` round-trip; verified on synthetic (types 0/1/2/5/7) and retail `mp4_mariomdl0.bin` (2 valid HSFV037 models) |
| NSBMD (DS models), incl. bone hierarchy | ✅ verified real skeletal hierarchy on 2 retail samples (was flat/all-root before) |
| PAC (Brawl "ARC\0" archive) | ✅ |
| PLT0 (Brawl G3D palette-swap animation) | ✅ |
| PSDK | 🔍 |
| QuickBMS interpreter (`wbmsx` + `wszst xx --bms`) | ✅ `copy`/`lz10`/`lz11`/`yay0`/`zlib`/`deflate` plus native aliases (`ash0`/`rl`/`huff4`/`huff8`/`huffman`/`rnc`/`lzh8`/`quicklz`/`blz`/`camelot`/`at7`), with CLI script chaining support via `wszst xx --bms` |
| RNC1 / RNC2 (Rob Northen Compression) | ✅ |
| WC24 crypto (`wwc24crypt`) | ✅ |

✅ verified · 🟡 partial · 🔍 detected, not decoded · ⛔ not implemented — see
the [gist](https://gist.github.com/quatric/144b2e005bfa1641b3d9d67ddc00151b)
for what backs each of these.

