#!/bin/bash
# Self-discovering regression for the Nintendo format additions.
# Finds samples by magic rather than by hardcoded path, so it keeps working
# after the scratch directories are cleaned.
cd "$(dirname "$0")/../project" || exit 1
B=./bin; PWD_PROJECT=$PWD; PASS=0; FAIL=0; SKIP=0
SEARCH=${SEARCH:-"/tmp $HOME/Downloads /Volumes/SSD/user/Downloads /Volumes/SSD/dlz/Folders"}
ok(){ printf "  PASS  %s\n" "$1"; PASS=$((PASS+1)); }
no(){ printf "  FAIL  %s -- %s\n" "$1" "$2"; FAIL=$((FAIL+1)); }
sk(){ printf "  SKIP  %s (no sample)\n" "$1"; SKIP=$((SKIP+1)); }

# Sample discovery. Probing every file for its magic is far too slow over a
# large external volume, so the index is built ONCE per run and each lookup is
# a table scan. Extension-filtered first, magic-confirmed second, so a
# mislabelled file is still classified by content. Set SEARCH to redirect.
IDX=$(mktemp); trap 'rm -f "$IDX"' EXIT
for d in $SEARCH; do [ -d "$d" ] || continue
  find -L "$d" -maxdepth 5 -type f -size -60M \( \
      -iname '*.bch' -o -iname '*.bcres' -o -iname '*.cgfx' -o -iname '*.nsbmd' \
      -o -iname '*.bfres' -o -iname '*.bntx' -o -iname '*.bmd' \
      -o -iname '*.plt0' -o -iname '*.pac' -o -iname '*.gfa' -o -iname '*.brfnt' \
      -o -iname '*.brsar' -o -iname '*.bffnt' -o -iname '*.bcfnt' \) 2>/dev/null
done | while IFS= read -r f; do
  printf '%s\t%s\n' "$(head -c 4 "$f" 2>/dev/null | tr -d '\0')" "$f"
done > "$IDX"
find_magic(){ awk -F'\t' -v m="$1" '$1==m{print $2; exit}' "$IDX"; }

t_model(){ # name magic
  local f; f=$(find_magic "$2"); [ -n "$f" ] || { sk "$1"; return; }
  rm -f /tmp/_r.dae
  $B/wmdlt ENCODE "$f" -d /tmp/_r.dae --overwrite >/dev/null 2>&1
  local g=$(grep -c '<geometry' /tmp/_r.dae 2>/dev/null||echo 0)
  [ "$g" -gt 0 ] && ok "$1 -> DAE ($g geometries)" || no "$1 -> DAE" "no geometry from $f"
}
t_img(){ # name magic
  local f; f=$(find_magic "$2"); [ -n "$f" ] || { sk "$1"; return; }
  # A magic-only stub proves nothing about decoding, and failing to decode one
  # is the correct behaviour -- so treat it as "no sample", not as a failure.
  if [ "$(stat -f%z "$f" 2>/dev/null || echo 0)" -lt 4096 ]; then
    printf "  SKIP  %s (only a magic-only stub available: %s)\n" "$1" "$f"
    SKIP=$((SKIP+1)); return
  fi
  rm -f /tmp/_r.png
  $B/wimgt ENCODE "$f" -d /tmp/_r.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r.png ] && ok "$1 -> PNG" || no "$1 -> PNG" "$f"
}

echo "== binaries =="
for x in wszst wimgt wmdlt wlayt wbmgt wbmsx wbrsar wajpg wlzh8 wwc24crypt; do
  [ -x "$B/$x" ] && ok "built: $x" || no "built: $x" "missing"
done

echo "== models =="
t_model "NSBMD (DS)"      "BMD0"
t_model "BCH (3DS)"       "BCH"
t_model "CGFX (3DS)"      "CGFX"

# "FRES" magic is shared by two unrelated formats: Wii U BFRES (big endian,
# version 3.x, ParseBFRES() in lib-bfres.c) and Switch BFRES (little endian,
# version 9+, structure-only extract_bfres_switch_manifest() in wszst.c --
# geometry decode is a known open gap, see PLAN.md). t_model() picking
# whichever "FRES" sample turns up first used to silently test the wrong
# parser whenever that sample happened to be a Switch file (real-world case:
# ~/Downloads/Male.bfres). Split by the byte-order-mark at offset 8 instead.
f_fres=$(find_magic "FRES")
bom8=$(od -An -tx1 -j8 -N2 "$f_fres" 2>/dev/null | tr -d ' ')
bomC=$(od -An -tx1 -j12 -N2 "$f_fres" 2>/dev/null | tr -d ' ')
if [ -n "$f_fres" ] && [ "$bom8" = "feff" ]; then
  t_model "BFRES (Wii U)" "FRES"
else
  sk "BFRES (Wii U)"
fi
if [ -n "$f_fres" ] && [ "$bomC" = "fffe" ]; then
  rm -f /tmp/_r_bfres_switch.xml
  "$B/wszst" xx "$f_fres" --dest /tmp/_r_bfres_switch.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<shape ' /tmp/_r_bfres_switch.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFRES (Switch) -> structure XML ($g shapes, $f_fres)" \
    || no "BFRES (Switch) -> structure XML" "no shapes from $f_fres"
else
  sk "BFRES (Switch)"
fi

# BCFNT (3DS, magic "CFNT")/BFFNT (Wii U, magic "FFNT") -- structure-only
# extract_cfnt_manifest() in wszst.c. See its comment for why this isn't
# reusable from the already-working BRFNT/BRFNA decode: the FINF pointer
# offsets differ from what hadashisora/NintyFont documents (verified +4
# against 2 real .bffnt samples) and TGLP's sheetFormat is a 3DS/Cafe GPU
# format id this fork has no decode table for, so pixel data is left alone.
f_ffnt=$(find_magic "FFNT"); f_cfnt=$(find_magic "CFNT")
if [ -n "$f_ffnt" ]; then
  rm -f /tmp/_r_bffnt.xml
  "$B/wszst" xx "$f_ffnt" --dest /tmp/_r_bffnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bffnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFFNT (Wii U) -> structure XML ($f_ffnt)" \
    || no "BFFNT (Wii U) -> structure XML" "no tglp from $f_ffnt"
else
  sk "BFFNT (Wii U)"
fi
if [ -n "$f_cfnt" ]; then
  rm -f /tmp/_r_bcfnt.xml
  "$B/wszst" xx "$f_cfnt" --dest /tmp/_r_bcfnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bcfnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BCFNT (3DS) -> structure XML ($f_cfnt)" \
    || no "BCFNT (3DS) -> structure XML" "no tglp from $f_cfnt"
else
  sk "BCFNT (3DS)"
fi

echo "== textures =="
t_img "BNTX (Switch)" "BNTX"

t_brres_tex_plt0(){
  # A BRRES TEX0 carries no palette of its own; a sibling PLT0 has to be
  # matched by naming convention and threaded through ExportPNG(). Curated
  # real sample (ins_taran.brres from a retail Animal Crossing: City Folk
  # disc, Insect/ins_taran.brres, TEX0+PLT0 same-name pair) because a
  # generic magic scan can't tell a paired BRRES from an unpaired one, and
  # this is exactly the bug the palette-pairing code exists to fix -- see
  # PLAN.md SS8 for the wider naming-convention picture and its known
  # remaining gap (variants that differ by a number, e.g. glow31/glow28,
  # aren't resolvable by filename and need the MDL0 material's sampler).
  local f="$HOME/Downloads/wszst-samples/accf_ins_taran.brres"
  [ -f "$f" ] || { sk "BRRES TEX0+PLT0 pairing (ACCF)"; return; }
  rm -rf /tmp/_r_texplt0; mkdir -p /tmp/_r_texplt0
  # XDECODE, not EXTRACT: EXTRACT alone dumps raw subfiles without image
  # transform, so it never exercises ExportPNG()'s palette-pairing at all.
  $B/wszst XDECODE "$f" --dest /tmp/_r_texplt0 --overwrite >/tmp/_r_texplt0.log 2>&1
  local png="/tmp/_r_texplt0/Textures(NW4R)/ins_taran.png"
  if [ -s "$png" ] && ! grep -q "INVALID IMAGE FORMAT" /tmp/_r_texplt0.log; then
    ok "BRRES TEX0+PLT0 pairing (ACCF) -> $png"
  else
    no "BRRES TEX0+PLT0 pairing (ACCF)" "$f"
  fi
}
t_brres_tex_plt0

t_plt0(){
  local f; f=$(find_magic "PLT0"); [ -n "$f" ] || { sk "PLT0 palette"; return; }
  rm -f /tmp/_r_plt0.png
  $B/wimgt DECODE "$f" -d /tmp/_r_plt0.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_plt0.png ] && ok "PLT0 palette -> PNG ($f)" || no "PLT0 palette -> PNG" "$f"
}
t_plt0

t_brfnt(){
  # BRFNT (Wii bitmap font, magic "RFNT"): glyph sheets use the normal GX
  # texture encodings (I4/IA4/etc, same as PLT0/TEX0), located by scanning
  # NFTR-family sections for TGLP rather than a fixed offset -- real fonts
  # vary in section order/count. Verified visually on 3 diverse retail
  # samples while developing this (I4 ASCII, IA4 outline, I4 Japanese
  # kana/katakana) -- all legible, correctly shaped glyphs, not noise.
  # `.brfna` (font *archive*) shares the "RFNT"-family scan but its
  # sheetCount field doesn't mean "this many contiguous physical sheets"
  # the way .brfnt's does -- confirmed on 2 real samples, both overflow the
  # file if taken literally -- so it's left rejecting rather than
  # misdecoding; not covered by this test.
  local f; f=$(find_magic "RFNT"); [ -n "$f" ] || { sk "BRFNT (Wii bitmap font)"; return; }
  rm -f /tmp/_r_brfnt.png /tmp/_r_brfnt.img000.png
  $B/wimgt DECODE "$f" -d /tmp/_r_brfnt.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_brfnt.img000.png ] && ok "BRFNT (Wii bitmap font) -> PNG ($f)" \
    || no "BRFNT (Wii bitmap font)" "$f"
}
t_brfnt

echo "== archives =="
t_pac(){
  # PAC's magic is "ARC\0"; the trailing NUL is stripped by the tr -d '\0'
  # step above, same as every other magic lookup here, so the index key is
  # the 3-byte "ARC".
  local f; f=$(find_magic "ARC"); [ -n "$f" ] || { sk "PAC (Brawl archive)"; return; }
  rm -rf /tmp/_r_pac; mkdir -p /tmp/_r_pac
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_pac/\1N" --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_pac -type f 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "PAC (Brawl archive) -> $n member(s) ($f)" || no "PAC (Brawl archive)" "$f"
}
t_pac

t_gfa(){
  # GFAC (Good-Feel archive); GFCP zip-mode 1 payloads are BPE-compressed.
  # The BPE decoder desynced on every real retail sample until it was fixed
  # against a live Kirby's Epic Yarn WBFS -- assert real decoded output
  # here, not just "some file got created", so a regression shows up as a
  # missing/empty member rather than a silent pass.
  local f; f=$(find_magic "GFAC"); [ -n "$f" ] || { sk "GFA (Good-Feel archive)"; return; }
  rm -rf /tmp/_r_gfa; mkdir -p /tmp/_r_gfa
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_gfa/\1N" --overwrite >/tmp/_r_gfa.log 2>&1
  local n; n=$(find /tmp/_r_gfa -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && ! grep -q "INVALID" /tmp/_r_gfa.log; then
    ok "GFA (Good-Feel archive) -> $n non-empty member(s) ($f)"
  else
    no "GFA (Good-Feel archive)" "$f"
  fi
}
t_gfa

echo "== compression round-trips =="
# The compression format is chosen by the DESTINATION EXTENSION, not a flag.
printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > /tmp/_r.bin
for e in lz10 lz11 rl yay0 ash0 lzh8 qlz; do
  rm -f /tmp/_r.$e /tmp/_r.out
  if $B/wszst COMPRESS /tmp/_r.bin --dest /tmp/_r.$e --overwrite >/dev/null 2>&1 \
  && $B/wszst DECOMPRESS /tmp/_r.$e --dest /tmp/_r.out --overwrite >/dev/null 2>&1 \
  && cmp -s /tmp/_r.bin /tmp/_r.out; then
    ok "$e round-trip ($(stat -f%z /tmp/_r.$e 2>/dev/null||echo ?) B)"
  else no "$e round-trip" "mismatch"; fi
done

echo "== BLZ (DS ARM9/ARM7/overlay compression) =="
t_blz(){
  # Real fixture, not synthetic bytes: 300-byte repeating pattern compressed
  # with the actual reference `blz -en` (CUE's tool, via
  # github.com/PeterLemon/Nintendo_DS_Compressors), base64-embedded since
  # there's no BLZ encoder in this tree (decode-only was the ask -- BLZ has
  # no header magic to safely auto-detect from, so it's dispatched by the
  # ".blz" source extension in decompress_nintendo_file(), not the usual
  # magic-table lookup; see the comment there). Verified against the real
  # reference decoder too, not just this round trip, while developing this.
  local d; d=$(mktemp -d)
  base64 -d > "$d/small.blz" <<'EOF'
DdAd8B3wHfAd8B3wHfAd8P8d8B3wHfAd8B3wHfAd8A3Q/wwNDg8AAQIDAAQFBgcICQoLADwAAAjwAAAA
EOF
  python3 -c "open('$d/expected.bin','wb').write(bytes(i%16 for i in range(300)))"
  "$B/wszst" DECOMPRESS "$d/small.blz" --dest "$d/out.bin" --overwrite >/dev/null 2>&1
  if cmp -s "$d/out.bin" "$d/expected.bin"; then
    ok "BLZ decompress (real reference-encoder fixture, 300 B)"
  else
    no "BLZ decompress" "mismatch"
  fi
  rm -rf "$d"
}
t_blz

echo "== wbmsx COMTYPE zlib/deflate =="
t_wbmsx_zlib(){
  command -v python3 >/dev/null || { sk "wbmsx COMTYPE zlib"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import zlib
raw = b'The quick brown fox jumps over the lazy dog. ' * 40
open('$d/expected.bin','wb').write(raw)
open('$d/zlib.bin','wb').write(zlib.compress(raw,9))
co = zlib.compressobj(9, zlib.DEFLATED, -15)
open('$d/deflate.bin','wb').write(co.compress(raw) + co.flush())
"
  local ok=1
  printf 'COMTYPE zlib\nCLOG "out.bin" 0 %d\n' "$(stat -f%z "$d/zlib.bin")" > "$d/zlib.bms"
  "$B/wbmsx" "$d/zlib.bms" "$d/zlib.bin" "$d/out_z" >/dev/null 2>&1
  cmp -s "$d/out_z/out.bin" "$d/expected.bin" || ok=0

  printf 'COMTYPE deflate\nCLOG "out.bin" 0 %d %d\n' \
    "$(stat -f%z "$d/deflate.bin")" "$(stat -f%z "$d/expected.bin")" > "$d/deflate.bms"
  "$B/wbmsx" "$d/deflate.bms" "$d/deflate.bin" "$d/out_d" >/dev/null 2>&1
  cmp -s "$d/out_d/out.bin" "$d/expected.bin" || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok_msg="wbmsx COMTYPE zlib+deflate round-trip" && ok "$ok_msg" \
    || no "wbmsx COMTYPE zlib+deflate round-trip" "mismatch"
}
t_wbmsx_zlib

echo "== wbmsx COMTYPE ash0/rl/lzh8/qlz (native decoders, not stock QuickBMS names) =="
t_wbmsx_native(){
  local d; d=$(mktemp -d)
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > "$d/expected.bin"
  local ok=1
  for e in ash0 rl lzh8 qlz; do
    rm -f "$d/f.$e"
    "$B/wszst" COMPRESS "$d/expected.bin" --dest "$d/f.$e" --overwrite >/dev/null 2>&1
    [ -s "$d/f.$e" ] || { ok=0; continue; }
    local ctype=$e; [ "$e" = "qlz" ] && ctype=quicklz
    printf 'COMTYPE %s\nCLOG "out.bin" 0 %d\n' "$ctype" "$(stat -f%z "$d/f.$e")" > "$d/f.bms"
    rm -rf "$d/out_$e"
    "$B/wbmsx" "$d/f.bms" "$d/f.$e" "$d/out_$e" >/dev/null 2>&1
    cmp -s "$d/out_$e/out.bin" "$d/expected.bin" || ok=0
  done
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "wbmsx COMTYPE ash0+rl+lzh8+quicklz round-trip" \
    || no "wbmsx COMTYPE ash0+rl+lzh8+quicklz round-trip" "mismatch"
}
t_wbmsx_native

echo "== Mario Party BIN (wmpbpack/wmpbdump, Hudson mpbin-tools port) =="
# No real Mario Party 4-8 disc image was available to test against (the only
# candidates found on this machine are truncated/fake rips, tens of MB where
# a real GC disc is ~1.4 GB) -- this is a synthetic round-trip only, not a
# real-sample verification. It still caught a real bug: both tools called
# getchar() on every error/warning path (ported straight from the original
# Windows console EXEs), which silently hangs forever under any script or
# CI runner with no output at all -- removed.
t_mpb(){
  local d; d=$(mktemp -d)
  printf 'AAAAAAAAAABBBBBBBBBBCCCCCCCCCC %.0s' {1..50} > "$d/in.dat"
  printf 'The quick brown fox. %.0s' {1..30} >> "$d/in.dat"
  local all_ok=1
  for ct in 0 1 2 5 7; do  # none, LZSS, YAZ0-like slide, RLE, inflate
    # wmpbdump's own success path returns 1, not 0 (ported as-is from the
    # original source) -- gate on the round-tripped bytes, not exit codes.
    ( cd "$d" && echo "compress_type=$ct: in.dat" > list.txt
      timeout 10 "$PWD_PROJECT/wmpbpack" list.txt "out$ct.bin" >/dev/null 2>&1
      timeout 10 "$PWD_PROJECT/wmpbdump" "out$ct.bin" >/dev/null 2>&1
      cmp -s "out${ct}_file0.dat" in.dat ) || all_ok=0
  done
  rm -rf "$d"
  [ "$all_ok" = 1 ] && ok "Mario Party BIN round-trip (compress_type 0/1/2/5/7, synthetic)" \
    || no "Mario Party BIN round-trip" "one or more compress_type mismatched"
}
t_mpb

echo "== recursive folder traversal (dclib ** wildcard) =="
# "recursive extraction" turned out to mean recursive directory traversal of
# CLI args, not recursing into nested archives. dclib's SearchPaths() already
# supports this via a shell-glob-style "**" pattern -- no new flag needed, we
# only need to prove it actually walks multiple directory levels and that a
# bare directory (no "**") is unaffected, matching existing behaviour.
RD=$(mktemp -d)
mkdir -p "$RD/a/b" "$RD/c"
for f in "$RD/top.bin" "$RD/a/one.bin" "$RD/a/b/two.bin" "$RD/c/three.bin"; do
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > "$f"
done
if $B/wszst COMPRESS "$RD/**/*.bin" --overwrite >/dev/null 2>&1 \
&& [ -f "$RD/top.szs" ] && [ -f "$RD/a/one.szs" ] \
&& [ -f "$RD/a/b/two.szs" ] && [ -f "$RD/c/three.szs" ]; then
  ok "COMPRESS 'dir/**/*.bin' reaches all 3 nesting levels"
else
  no "COMPRESS 'dir/**/*.bin'" "not all nested files were compressed"
fi
rm -f "$RD/top.bin" "$RD/a/one.bin" "$RD/a/b/two.bin" "$RD/c/three.bin"
if $B/wszst DECOMPRESS "$RD/**/*.szs" --overwrite >/dev/null 2>&1 \
&& [ -f "$RD/top.bin" ] && [ -f "$RD/a/one.bin" ] \
&& [ -f "$RD/a/b/two.bin" ] && [ -f "$RD/c/three.bin" ]; then
  ok "DECOMPRESS 'dir/**/*.szs' reaches all 3 nesting levels"
else
  no "DECOMPRESS 'dir/**/*.szs'" "not all nested files were decompressed"
fi
# Baseline: passing a bare directory (no "**") must NOT silently recurse.
if $B/wszst DECOMPRESS "$RD" >/dev/null 2>&1; then
  no "bare directory arg (non-recursive baseline)" "should have failed, not silently recursed"
else
  ok "bare directory arg still fails (non-recursive behaviour unchanged)"
fi
rm -rf "$RD"

echo "== external pass-through extraction (wit/ndstool/ctrtool/sharpii) =="
# Find one real .nds and one real .wad under SEARCH; a plain PATH lookup for
# the external tool decides skip vs. pass/fail, same convention as t_model.
PT_NDS=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 4 -iname '*.nds' -size -60M -print -quit 2>/dev/null; done | head -1)
PT_WAD=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 4 -iname '*.wad' -size -60M -print -quit 2>/dev/null; done | head -1)

if command -v ndstool >/dev/null 2>&1 && [ -n "$PT_NDS" ]; then
  RD=$(mktemp -d); cp "$PT_NDS" "$RD/t.nds"
  $B/wszst XX "$RD/t.nds" >/dev/null 2>&1
  if [ -f "$RD/t.d/arm9.bin" ] && [ -f "$RD/t.d/arm7.bin" ] && [ -d "$RD/t.d/data" ] \
  && [ ! -d "$RD/t.d/rominfo.xml" ]; then
    ok "NDS pass-through (ndstool): arm9/arm7/data staged"
  else
    no "NDS pass-through (ndstool)" "expected staged files missing under $RD/t.d"
  fi
  rm -rf "$RD"
else sk "NDS pass-through (ndstool)"; fi

if command -v sharpii >/dev/null 2>&1 && [ -n "$PT_WAD" ]; then
  RD=$(mktemp -d); cp "$PT_WAD" "$RD/t.wad"
  $B/wszst XX "$RD/t.wad" >/dev/null 2>&1
  # A real WAD's *.app content files must NOT be re-claimed as nested WADs
  # (they're raw ELF/binary payloads sharpii itself produced) -- this is the
  # false-positive this section exists to catch, see lib-passthru.c.
  APP_COUNT=$(find "$RD/t.d" -maxdepth 1 -iname '*.app' 2>/dev/null | wc -l | tr -d ' ')
  if [ "${APP_COUNT:-0}" -gt 0 ] && ! grep -rq "SHARPII_NET_CORE_WAD_UNKNOWN" "$RD" 2>/dev/null; then
    ok "WAD pass-through (sharpii): $APP_COUNT .app content file(s) staged, none mis-reclaimed"
  else
    no "WAD pass-through (sharpii)" "no staged .app content, or a content file was wrongly re-dispatched as a WAD"
  fi
  rm -rf "$RD"
else sk "WAD pass-through (sharpii)"; fi

echo "== BRSAR (via wbrsar, statically-linked vgmtrans) =="
t_brsar(){
  # RSARScanner (and every other vgmtrans format scanner) self-registers
  # purely via a global-constructor side effect -- nothing else in the
  # program calls into its .o by symbol reference. A plain static-library
  # link only pulls in .o members that resolve an unresolved symbol, so
  # the linker was silently DROPPING the entire scanner and its
  # registration constructor never ran: confirmed with `nm` (zero
  # RSARScanner symbols in the linked binary) and functionally (every
  # real .brsar sample tried failed identically with "no collections
  # found", across 4 different retail games) -- wbrsar's BRSAR support
  # had never actually executed since it was added to this fork, not a
  # format-parsing bug. Fixed by force-loading the whole archive
  # (-Wl,-force_load on mac, --whole-archive elsewhere) in the Makefile's
  # VGMTRANS_LIBS. This test is the regression guard for that: a real
  # retail sample producing a real, valid Standard MIDI File (magic
  # "MThd", parseable header), not just "some file got created."
  # Not every real .brsar has RSEQ (sequence) sounds -- some banks are
  # SFX/WAVE-only -- so try each magic-matched candidate in turn rather
  # than just the first, same as find_magic() would give.
  local candidates; candidates=$(awk -F'\t' '$1=="RSAR"{print $2}' "$IDX")
  [ -n "$candidates" ] || { sk "BRSAR (wbrsar)"; return; }
  local f mid
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -rf /tmp/_r_brsar; mkdir -p /tmp/_r_brsar
    $B/wbrsar "$f" /tmp/_r_brsar >/tmp/_r_brsar.log 2>&1
    mid=$(find /tmp/_r_brsar -iname "*.mid" -size +14c 2>/dev/null | head -1)
    [ -n "$mid" ] && [ "$(head -c4 "$mid")" = "MThd" ] && break
    mid=""
  done <<< "$candidates"
  if [ -n "$mid" ]; then
    ok "BRSAR -> MIDI ($f)"
  else
    no "BRSAR -> MIDI" "no candidate produced a valid MIDI"
  fi
}
t_brsar

echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
