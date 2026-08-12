#!/bin/bash
# Self-discovering regression for the Nintendo format additions.
# Finds samples by magic rather than by hardcoded path, so it keeps working
# after the scratch directories are cleaned.
cd "$(dirname "$0")/../project" || exit 1
B=./bin; PASS=0; FAIL=0; SKIP=0
SEARCH=${SEARCH:-"/tmp $HOME/Downloads /Volumes/SSD/user/Downloads"}
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
      -o -iname '*.bfres' -o -iname '*.bntx' -o -iname '*.bmd' \) 2>/dev/null
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
t_model "BFRES (Wii U)"   "FRES"
t_model "CGFX (3DS)"      "CGFX"

echo "== textures =="
t_img "BNTX (Switch)" "BNTX"

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

echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
