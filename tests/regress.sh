#!/bin/bash
# Self-discovering regression for the Nintendo format additions.
# Finds samples by magic rather than by hardcoded path, so it keeps working
# after the scratch directories are cleaned.
cd "$(dirname "$0")/../project" || exit 1
B=./bin; PASS=0; FAIL=0; SKIP=0
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
      -o -iname '*.plt0' -o -iname '*.pac' \) 2>/dev/null
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

t_plt0(){
  local f; f=$(find_magic "PLT0"); [ -n "$f" ] || { sk "PLT0 palette"; return; }
  rm -f /tmp/_r_plt0.png
  $B/wimgt DECODE "$f" -d /tmp/_r_plt0.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_plt0.png ] && ok "PLT0 palette -> PNG ($f)" || no "PLT0 palette -> PNG" "$f"
}
t_plt0

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

echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
