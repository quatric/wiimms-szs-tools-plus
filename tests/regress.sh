#!/bin/bash
export LC_ALL=C
# Self-discovering regression for the Nintendo format additions.
# Finds samples by magic rather than by hardcoded path, so it keeps working
# after the scratch directories are cleaned.
cd "$(dirname "$0")/../project" || exit 1
B=./bin; PWD_PROJECT=$PWD; DAE_VALIDATOR="$PWD_PROJECT/../tests/validate-dae.py"; PASS=0; FAIL=0; SKIP=0
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
  # Exclude claude-* session scratch dirs and this script's own throwaway
  # fixture basenames (test.*/test_*) -- both classes of file legitimately
  # match these extensions but are synthetic, single-glyph/tiny artifacts
  # from manual dev sessions, not real retail samples, and can otherwise
  # get matched by find_magic() ahead of a genuine sample and fail the
  # "suspiciously small/blank" heuristic for reasons that have nothing to
  # do with the decoder under test.
  find -L "$d" -maxdepth 8 -type f -size -65M \
      ! -path '*claude-*' ! -path '*/_r_*' ! -iname '_r_*' ! -iname 'test.*' ! -iname 'test_*' \( \
      -iname '*.bch' -o -iname '*.bcres' -o -iname '*.cgfx' -o -iname '*.nsbmd' \
      -o -iname '*.bfres' -o -iname '*.bntx' -o -iname '*.bmd' -o -iname '*.gtx' \
      -o -iname '*.plt0' -o -iname '*.pac' -o -iname '*.gfa' -o -iname '*.brfnt' \
      -o -iname '*.brfna' -o -iname '*.ctpk' -o -iname '*.warc' \
      -o -iname '*.byml' -o -iname '*.byaml' -o -iname '*.narc' \
      -o -iname '*.brsar' -o -iname '*.bffnt' -o -iname '*.bcfnt' \
      -o -iname '*.brlan' -o -iname '*.brlyt' -o -iname '*.bmg' \) 2>/dev/null
done | while IFS= read -r f; do
  printf '%s\t%s\n' "$(head -c 4 "$f" 2>/dev/null | tr -d '\0')" "$f"
done > "$IDX"
find_magic(){ awk -F'\t' -v m="$1" '$1==m || index($1,m)==1{print $2; exit}' "$IDX"; }

t_model(){ # name magic
  local found=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -f /tmp/_r.dae
    $B/wmdlt ENCODE "$f" -d /tmp/_r.dae --overwrite >/dev/null 2>&1
    local g; g=$(grep -c '<geometry' /tmp/_r.dae 2>/dev/null || true); g=${g:-0}
    if [ "$g" -gt 0 ] 2>/dev/null && python3 "$DAE_VALIDATOR" /tmp/_r.dae >/dev/null 2>&1; then
      ok "$1 -> DAE ($g geometries, validated)"
      found=1
      break
    fi
  done < <(awk -F'\t' -v m="$2" '$1==m{print $2}' "$IDX")
  [ "$found" -eq 1 ] || {
    local first; first=$(find_magic "$2")
    [ -n "$first" ] && no "$1 -> DAE" "no valid geometry from $first" || sk "$1"
  }
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
for x in wszst wimgt wmdlt wlayt wbmgt wbmsx wbrsar wwc24crypt; do
  [ -x "$B/$x" ] && ok "built: $x" || no "built: $x" "missing"
done

# Address vectors generated independently with GTX-Extractor's Python
# AddrLib port. --gc-sections lets this tiny test link only the surface
# addressing API from the normal production object.
if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-address.c ./lib-gtx.o -Wl,--gc-sections \
    -o /tmp/_r_gtx_address >/tmp/_r_gtx_address_build.log 2>&1 \
    && /tmp/_r_gtx_address; then
  ok "GX2 macro-tile address vectors (modes 4-11)"
else
  no "GX2 macro-tile address vectors (modes 4-11)" \
    "$(tail -1 /tmp/_r_gtx_address_build.log 2>/dev/null)"
fi

if ${CC:-cc} -O2 -ffunction-sections -fdata-sections -Isrc -Idclib \
    ../tests/test-gtx-encode.c ./lib-gtx.o ./lib-bntx.o -Wl,--gc-sections \
    -o /tmp/_r_gtx_encode >/tmp/_r_gtx_encode_build.log 2>&1 \
    && /tmp/_r_gtx_encode; then
  ok "GX2 encode/decode: formats, tile modes, mips, arrays and MSAA"
else
  no "GX2 general encoder matrix" \
    "$(tail -1 /tmp/_r_gtx_encode_build.log 2>/dev/null)"
fi

echo "== models =="
t_model "NSBMD (DS)"      "BMD0"
t_model "BCH (3DS)"       "BCH"
t_cgfx(){
  # CGFX (3DS model container, .bcmdl extension in retail SZS archives):
  # a real sample from a retail 3DS RomFS -- Super Mario 3D Land's
  # ObjectData/TogeMetbo.szs, decompressed with wszst EXTRACT -- since a
  # banner.bin-embedded CGFX (the CBMD 3D banner resource shipped in every
  # CIA's ExeFS) turned out to use a structurally different, non-standard
  # header layout ScanCGFX() doesn't recognise (DATA block one byte off
  # from where the header's own hdr_len field points), while this
  # standalone in-SZS .bcmdl matches the format exactly.
  local f="$PWD_PROJECT/../tests/fixtures/cgfx_toge_metbo.bcmdl"
  if [ -f "$f" ]; then
    rm -f /tmp/_r.dae
    $B/wmdlt ENCODE "$f" -d /tmp/_r.dae --overwrite >/dev/null 2>&1
    local g; g=$(grep -c '<geometry' /tmp/_r.dae 2>/dev/null || true); g=${g:-0}
    if [ "$g" -gt 0 ] 2>/dev/null && python3 "$DAE_VALIDATOR" /tmp/_r.dae >/dev/null 2>&1; then
      ok "CGFX (3DS) -> DAE ($g geometries, validated, $f)"
      return
    fi
    no "CGFX (3DS) -> DAE" "no valid geometry from $f"
    return
  fi
  t_model "CGFX (3DS)" "CGFX"
}
t_cgfx

t_bch_dae_texture(){
  local f="$HOME/Downloads/aaaaa/live1/h3d/Mii_body.bch"
  [ -f "$f" ] || f=$(find_magic "BCH")
  [ -n "$f" ] && [ -f "$f" ] || { sk "BCH (3DS) model + texture export"; return; }

  local out
  out=$(mktemp -d /tmp/_r_bch_tex.XXXXXX) || { no "BCH model + texture export" "mktemp failed"; return; }
  $B/wmdlt ENCODE "$f" -d "$out/model.dae" --overwrite >/dev/null 2>&1
  local dae="$out/model.dae"
  local geom mat img tri
  geom=$(grep -c '<geometry ' "$dae" 2>/dev/null || true); geom=${geom:-0}
  mat=$(grep -c '<material ' "$dae" 2>/dev/null || true); mat=${mat:-0}
  img=$(grep -c '<image ' "$dae" 2>/dev/null || true); img=${img:-0}
  tri=$(grep -c '<triangles ' "$dae" 2>/dev/null || true); tri=${tri:-0}
  local png_cnt
  png_cnt=$(find "$out" -name '*.png' 2>/dev/null | wc -l | tr -d ' ')
  local val_ok=0
  if python3 "$DAE_VALIDATOR" --require-images "$dae" >/dev/null 2>&1; then
    val_ok=1
  fi

  if [ -s "$dae" ] && [ "$geom" -gt 0 ] && [ "$mat" -gt 0 ] && [ "$img" -gt 0 ] && [ "$tri" -gt 0 ] && [ "$png_cnt" -gt 0 ] && [ "$val_ok" -eq 1 ]; then
    ok "BCH (3DS) -> DAE + texture mapping ($geom geoms, $mat mats, $img imgs, $png_cnt textures)"
  else
    no "BCH (3DS) -> DAE + texture mapping" "$f"
  fi
  rm -rf "$out"
}
t_bch_dae_texture


# "FRES" magic is shared by two unrelated formats: Wii U BFRES (big endian,
# version 3.x, ParseBFRES() in lib-bfres.c) and Switch BFRES (little endian,
# version 8+, extract_bfres_switch_manifest() in wszst.c -- name/shape/
# material resolution is decoded and verified against real retail data
# (see the long comment above that function for what changed and why),
# vertex/index geometry decode is still a known open gap, see PLAN.md).
# t_model() picking whichever "FRES" sample turns up first used to silently
# test the wrong parser whenever that sample happened to be a Switch file
# (real-world case: ~/Downloads/Male.bfres). Wii U discovery keeps the
# original single-first-match lookup unchanged (out of scope for this
# Switch-focused fix). Switch discovery instead scans *every* "FRES" match
# for the little-endian BOM, since with a Wii U sample also present in
# SEARCH the single first-hit lookup could land on that Wii U file and skip
# Switch entirely even though a real Switch sample -- e.g.
# ~/Downloads/Male.bfres or ~/Downloads/SMO_AirBubble.bfres, a real Super
# Mario Odyssey ObjectData sample kept as a stable regression fixture since
# the fork's own extraction of Nintendo's RomFS obviously can't be
# committed to the repo -- was sitting right there.
t_bfres_wiiu(){
  # Wii U BFRES geometry decode, gated to the big-endian (BOM "feff",
  # version 3.x) container -- see the long comment above for why "FRES"
  # magic alone isn't enough to pick the right parser. find_magic("FRES")
  # over $SEARCH previously landed on whichever "FRES" sample sorted first
  # in the IDX table, which was a coin flip between this Wii U format and
  # the little-endian Switch one whenever both were present, so this used
  # to SKIP even though a working Wii U sample (Splatoon's
  # SPL_Clt_TES011_M.bfres, also used by the texture-binding test below)
  # was sitting right there. Committed as a fixture for a deterministic,
  # order-independent pick.
  local f="$PWD_PROJECT/../tests/fixtures/bfres_wiiu_splatoon_clt.bfres"
  if [ -f "$f" ]; then
    local bom8; bom8=$(od -An -tx1 -j8 -N2 "$f" 2>/dev/null | tr -d ' ')
    if [ "$bom8" = "feff" ]; then
      rm -f /tmp/_r.dae
      $B/wmdlt ENCODE "$f" -d /tmp/_r.dae --overwrite >/dev/null 2>&1
      local g; g=$(grep -c '<geometry' /tmp/_r.dae 2>/dev/null || true); g=${g:-0}
      if [ "$g" -gt 0 ] 2>/dev/null && python3 "$DAE_VALIDATOR" /tmp/_r.dae >/dev/null 2>&1; then
        ok "BFRES (Wii U) -> DAE ($g geometries, validated, $f)"
        return
      fi
      no "BFRES (Wii U) -> DAE" "no valid geometry from $f"
      return
    fi
  fi
  # Fixture missing/invalid -- fall back to the original dynamic scan.
  local f_fres; f_fres=$(find_magic "FRES")
  local bom8; bom8=$(od -An -tx1 -j8 -N2 "$f_fres" 2>/dev/null | tr -d ' ')
  if [ -n "$f_fres" ] && [ "$bom8" = "feff" ]; then
    t_model "BFRES (Wii U)" "FRES"
  else
    sk "BFRES (Wii U)"
  fi
}
t_bfres_wiiu

t_bfres_texture(){
  # BFRES (Wii U) material -> FTEX texture binding: wszst xx must decode
  # the referenced FTEX to a sibling PNG AND the exported DAE must
  # reference it by name in a non-empty <library_images>. A real sample
  # with a resolvable diffuse texture ref is kept at ~/Downloads/
  # bfres_samples/ since the fork's own disc extractions can't be
  # committed to the repo.
  local f
  f=$(find -L "$HOME/Downloads/bfres_samples" -iname '*.bfres' 2>/dev/null | head -1)
  [ -n "$f" ] || { sk "BFRES (Wii U) texture binding"; return; }
  rm -rf /tmp/_r_bfrestex; mkdir -p /tmp/_r_bfrestex
  cp "$f" /tmp/_r_bfrestex/
  local bf="/tmp/_r_bfrestex/$(basename "$f")"
  "$B/wszst" xx "$bf" --overwrite >/dev/null 2>&1
  local glb="${bf}.glb"
  local png_n; png_n=$(find /tmp/_r_bfrestex -iname '*.png' 2>/dev/null | wc -l | tr -d ' ')
  local img_n; img_n=$(python3 -c "
import sys; sys.path.insert(0,'$PWD_PROJECT/../tests')
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location('vglb','$PWD_PROJECT/../tests/validate-glb.py')
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb('$glb')
    print(len(g.json.get('images', [])))
except Exception:
    print(0)
" 2>/dev/null)
  img_n=${img_n:-0}
  if [ "$png_n" -gt 0 ] && [ "$img_n" -gt 0 ]; then
    ok "BFRES (Wii U) texture binding -> $png_n PNG(s), $img_n image(s) ($f)"
  else
    no "BFRES (Wii U) texture binding" "$png_n PNG(s), $img_n image(s) from $f"
  fi
}
t_bfres_texture

t_romc(){
  # N64 Virtual Console "romc" ROM compression -- a real, gated-by-bare-
  # filename decoder (see decode_romc_if_possible()'s comment in wszst.c
  # for why: no magic, no extension, just the literal name "romc" as found
  # in a real retail Kirby 64 (USA) VC WAD). Sample kept at ~/Downloads/
  # vc_samples/ since a whole WAD can't be committed to the repo.
  local f="$HOME/Downloads/vc_samples/Kirby64_romc"
  [ -f "$f" ] || { sk "romc (N64 Virtual Console)"; return; }
  rm -rf /tmp/_r_romc; mkdir -p /tmp/_r_romc
  cp "$f" /tmp/_r_romc/romc
  "$B/wszst" xx /tmp/_r_romc/romc --overwrite >/dev/null 2>&1
  local out="/tmp/_r_romc/romc.z64"
  if [ -s "$out" ] && [ "$(head -c4 "$out" | od -An -tx1 | tr -d ' \n')" = "80371240" ]; then
    ok "romc (N64 Virtual Console) -> real N64 ROM ($(stat -f%z "$out" 2>/dev/null || stat -c%s "$out") bytes)"
  else
    no "romc (N64 Virtual Console)" "no valid N64 ROM produced from $f"
  fi
}
t_romc

f_fres_switch=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  bomC=$(od -An -tx1 -j12 -N2 "$f" 2>/dev/null | tr -d ' ')
  [ "$bomC" = "fffe" ] && { f_fres_switch="$f"; break; }
done < <(awk -F'\t' -v m="FRES" '$1==m{print $2}' "$IDX")

if [ -n "$f_fres_switch" ]; then
  rm -f /tmp/_r_bfres_switch.xml
  "$B/wszst" xx "$f_fres_switch" --dest /tmp/_r_bfres_switch.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<shape name="[^"]' /tmp/_r_bfres_switch.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFRES (Switch) -> structure XML with resolved names ($g shapes, $f_fres_switch)" \
    || no "BFRES (Switch) -> structure XML" "no named shapes from $f_fres_switch"
else
  sk "BFRES (Switch)"
fi

# BCFNT (3DS, magic "CFNT")/BFFNT (Wii U, magic "FFNT") -- structure XML +
# pixel decode. AssignIMG's NFMT_BCFNT branch handles pixel data; format 0
# (RGBA8 linear, as written by EncodeBCFNT_RGBA) decodes by direct copy.
f_ffnt=$(find_magic "FFNT"); f_cfnt=$(find_magic "CFNT")
# Keep the 3DS decoder check deterministic even when no retail CFNT happens
# to exist under SEARCH. This compact fixture is produced by the encoder
# round-trip test below and exercises the same structure + pixel decoders.
if [ -z "$f_cfnt" ]; then
  f_cfnt="$PWD_PROJECT/../tests/fixtures/synthetic_rgba8.bcfnt"
  [ -f "$f_cfnt" ] || f_cfnt=""
fi
if [ -n "$f_ffnt" ]; then
  rm -f /tmp/_r_bffnt.xml /tmp/_r_bffnt.png
  "$B/wszst" xx "$f_ffnt" --dest /tmp/_r_bffnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bffnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BFFNT (Wii U) -> structure XML ($f_ffnt)" \
    || no "BFFNT (Wii U) -> structure XML" "no tglp from $f_ffnt"
  "$B/wimgt" DECODE "$f_ffnt" --dest /tmp/_r_bffnt.png --overwrite >/dev/null 2>&1
  psz=$(stat -c%s /tmp/_r_bffnt.png 2>/dev/null || echo 0)
  [ "$psz" -gt 100 ] 2>/dev/null && ok "BFFNT (Wii U) -> PNG ($f_ffnt)" \
    || no "BFFNT (Wii U) -> PNG" "decode produced no PNG (size=$psz)"
else
  sk "BFFNT (Wii U)"
fi
if [ -n "$f_cfnt" ]; then
  rm -f /tmp/_r_bcfnt.xml /tmp/_r_bcfnt.png
  "$B/wszst" xx "$f_cfnt" --dest /tmp/_r_bcfnt.xml --overwrite >/dev/null 2>&1
  g=$(grep -c '<tglp ' /tmp/_r_bcfnt.xml 2>/dev/null || echo 0)
  [ "$g" -gt 0 ] 2>/dev/null && ok "BCFNT (3DS) -> structure XML ($f_cfnt)" \
    || no "BCFNT (3DS) -> structure XML" "no tglp from $f_cfnt"
  "$B/wimgt" DECODE "$f_cfnt" --dest /tmp/_r_bcfnt.png --overwrite >/dev/null 2>&1
  psz=$(stat -c%s /tmp/_r_bcfnt.png 2>/dev/null || echo 0)
  [ "$psz" -gt 100 ] 2>/dev/null && ok "BCFNT (3DS) -> PNG ($f_cfnt)" \
    || no "BCFNT (3DS) -> PNG" "decode produced no PNG (size=$psz)"
else
  sk "BCFNT (3DS)"
fi

echo "== textures =="
t_img "BNTX (Switch)" "BNTX"
t_img "GTX (Wii U)" "Gfx2"

t_bntx_astc(){
  # Curated real ASTC_4x4 sample retained as an end-to-end container/swizzle/
  # codec regression. The decoder also accepts every standard 2D ASTC block
  # footprint plus BC6H and BC7; their codec cores have separate upstream
  # conformance vectors. Index 0 of this BNTX is a 1280x720
  # ASTC_4x4 texture sliced straight out of Odyssey's
  # LayoutData/TextureHintPhotoOther2.szs -> .bfres -> embedded BNTX (the
  # "hint" UI icon strip for the Moon Kingdom's pillar puzzle). find_magic's
  # generic BNTX sample may not happen to be ASTC, so this is pinned by path
  # like the accf_ins_taran BRRES/PLT0 sample above.
  local f="$PWD_PROJECT/../tests/fixtures/smo_hint_photo_astc.bntx"
  [ -f "$f" ] || { sk "BNTX ASTC_4x4 (Switch, SMO)"; return; }
  rm -f /tmp/_r_astc.png
  $B/wimgt DECODE "$f" -d /tmp/_r_astc.png --overwrite >/tmp/_r_astc.log 2>&1
  # A broken decode still emits a same-sized PNG (ASTC error blocks are
  # opaque magenta per spec), so check for structure, not just non-empty:
  # the real sample carries a light-grey background plus a solid-yellow
  # banana icon, i.e. at least a few dozen distinct colours, not a flat fill.
  local colors
  colors=$(python3 -c "
import sys
try:
    from PIL import Image
    im = Image.open('/tmp/_r_astc.png').convert('RGB')
    print(len(im.getcolors(maxcolors=1000000) or []))
except Exception:
    print(0)
" 2>/dev/null)
  if [ -s /tmp/_r_astc.png ] && [ "${colors:-0}" -gt 20 ]; then
    ok "BNTX ASTC_4x4 (Switch, SMO) -> PNG ($colors colours)"
  else
    no "BNTX ASTC_4x4 (Switch, SMO) -> PNG" "$f"
  fi
}
t_bntx_astc

t_brres_tex_plt0(){
  # A BRRES TEX0 carries no palette of its own; a sibling PLT0 has to be
  # matched by naming convention and threaded through ExportPNG(). Curated
  # real sample (ins_taran.brres from a retail Animal Crossing: City Folk
  # disc, Insect/ins_taran.brres, TEX0+PLT0 same-name pair) because a
  # generic magic scan can't tell a paired BRRES from an unpaired one, and
  # this is exactly the bug the palette-pairing code exists to fix -- see
  # PLAN.md SS8 for the wider naming-convention picture. MDL0 sampler links
  # are preferred when present; constrained retail naming fallbacks cover
  # archives that leave those offsets unresolved.
  local f="$PWD_PROJECT/../tests/fixtures/accf_ins_taran.brres"
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

  # BRRES puts MDL0 files under 3DModels(NW4R), while decoded TEX0 images
  # live in sibling Textures(NW4R). The exporter's default model format is
  # GLB (commit ac04d55), which embeds textures directly in the binary --
  # so there is no cross-directory init_from path to worry about any more;
  # instead assert the embedded image bytes are present and PNG-shaped.
  # (A loose ins_taran.png is still written by XDECODE/TEX0 export itself,
  # independent of the model exporter -- checked separately above.)
  rm -rf /tmp/_r_brres_dae; mkdir -p /tmp/_r_brres_dae
  $B/wszst XX "$f" --dest /tmp/_r_brres_dae --overwrite >/tmp/_r_brres_dae.log 2>&1
  local glb="/tmp/_r_brres_dae/3DModels(NW4R)/ins_taran.glb"
  local glb_report
  glb_report=$(python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb(sys.argv[1])
except Exception as exc:
    print(f"PARSE_FAIL {exc}"); sys.exit(0)
errs = m.validate(sys.argv[1], require_images=True)
j = g.json
meshes = j.get("meshes", [])
mesh_names = sorted(me.get("name") for me in meshes)
mat_names = sorted(mt.get("name") for mt in j.get("materials", []))
img_names = [im.get("name") for im in j.get("images", [])]
skins = j.get("skins", [])
n_skinned_prims = sum(
    1 for me in meshes for p in me.get("primitives", [])
    if "JOINTS_0" in p.get("attributes", {}) and "WEIGHTS_0" in p.get("attributes", {})
)
texcoord_ok = all(
    "TEXCOORD_0" in p.get("attributes", {})
    for me in meshes for p in me.get("primitives", [])
)
mat_bound_ok = all(
    "material" in p
    for me in meshes for p in me.get("primitives", [])
)
# 0.128204/0.222473 is the retail ins_taran MDL0's known-good first UV
# coordinate in COLLADA's bottom-left-origin V convention. glTF uses a
# top-left-origin V (the spec-mandated flip), so the same texel is
# 0.128204/(1-0.222473) here -- present in the TEXCOORD_0 accessor if
# decode is correct.
found_known_vertex = False
for acc_idx, acc in enumerate(j.get("accessors", [])):
    if acc.get("type") == "VEC2" and acc.get("componentType") == 5126:
        for tup in g.read_accessor_tuples(acc_idx):
            if abs(tup[0] - 0.128204) < 1e-4 and abs(tup[1] - (1 - 0.222473)) < 1e-4:
                found_known_vertex = True
                break
    if found_known_vertex:
        break
print("OK" if not errs else "VALIDATE_FAIL " + "; ".join(errs))
print(f"meshes={mesh_names}")
print(f"materials={mat_names}")
print(f"images={img_names}")
print(f"skins={len(skins)}")
print(f"skinned_prims={n_skinned_prims}")
print(f"texcoord_ok={texcoord_ok}")
print(f"mat_bound_ok={mat_bound_ok}")
print(f"found_known_vertex={found_known_vertex}")
PY
)
  if [ -s "$glb" ] \
      && echo "$glb_report" | grep -q '^OK$' \
      && echo "$glb_report" | grep -q "meshes=\['polygon0', 'polygon1'\]" \
      && echo "$glb_report" | grep -q "materials=\['m0', 'm1'\]" \
      && echo "$glb_report" | grep -q "images=\['ins_taran', 'ins_taran'\]" \
      && echo "$glb_report" | grep -q "skins=1" \
      && echo "$glb_report" | grep -q "skinned_prims=2" \
      && echo "$glb_report" | grep -q "texcoord_ok=True" \
      && echo "$glb_report" | grep -q "mat_bound_ok=True" \
      && echo "$glb_report" | grep -q "found_known_vertex=True"; then
    ok "BRRES MDL0 geometry + material/UV/texture mapping (GLB)"
  else
    no "BRRES MDL0 geometry + material/UV/texture mapping" "$f ($glb_report)"
  fi

  # A directory supplied directly to XX must take the same guarded recursive
  # path as a pass-through extractor's staging directory. Use mktemp rather
  # than a fixed directory so this test never consumes or removes user files.
  local tree
  tree=$(mktemp -d /tmp/_r_xx_dir.XXXXXX) || { no "XX directory recursion" "mktemp failed"; return; }
  cp "$f" "$tree/accf_ins_taran.brres"
  $B/wszst XX "$tree" --overwrite >/tmp/_r_xx_dir.log 2>&1
  glb="$tree/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.glb"
  if [ -s "$glb" ] && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1; then
    ok "XX directory recursion -> nested BRRES GLB"
  else
    no "XX directory recursion" "$tree"
  fi

  # A pass-through disc extractor supplies both a completed staging directory
  # and the user's --dest. The final model pass must keep each GLB beside its
  # MDL0; applying --dest again collapses thousands of same-named resources
  # into one flat directory (and historically stripped the .dae/.glb suffix).
  local staged
  staged=$(mktemp -d /tmp/_r_xx_dest.XXXXXX) || { no "XX staged tree with --dest" "mktemp failed"; return; }
  mkdir -p "$staged/source/nested" "$staged/destination"
  cp "$f" "$staged/source/nested/accf_ins_taran.brres"
  $B/wszst XX "$staged/source" --dest "$staged/destination" --overwrite >/dev/null 2>&1
  glb="$staged/source/nested/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.glb"
  if [ -s "$glb" ] && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && [ ! -e "$staged/destination/ins_taran" ]; then
    ok "XX staged tree + --dest -> preserves nested GLB path"
  else
    no "XX staged tree with --dest" "$staged"
  fi
}
t_brres_tex_plt0

t_accf_bind_pose(){
  # MDL0 stores rigid/single-bone vertices in bone-local coordinates, while
  # multi-weight vertices use a skinning node. Treating both alike produces
  # valid XML containing visibly collapsed geometry. These two retail models
  # cover both per-facepoint matrix selection and a rigid object node.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local spider_src="$sample_dir/accf_ins_taran.brres"
  local npc_src="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$spider_src" ] || [ ! -f "$npc_src" ]; then
    sk "ACCF MDL0 bind-pose transforms"; return
  fi
  local out
  out=$(mktemp -d /tmp/_r_accf_bind.XXXXXX) || { no "ACCF MDL0 bind-pose transforms" "mktemp failed"; return; }
  $B/wszst XX "$spider_src" --dest "$out/spider" --overwrite >/dev/null 2>&1
  $B/wszst XX "$npc_src" --dest "$out/npc" --overwrite >/dev/null 2>&1
  local spider="$out/spider/3DModels(NW4R)/ins_taran.glb"
  local npc="$out/npc/3DModels(NW4R)/tti.glb"
  if python3 - "$spider" "$npc" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)

def inspect(path):
    g = m.load_glb(path)
    j = g.json
    points = []
    for mesh in j.get("meshes", []):
        for prim in mesh.get("primitives", []):
            pos_idx = prim.get("attributes", {}).get("POSITION")
            if pos_idx is not None:
                points.extend(g.read_accessor_tuples(pos_idx))
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    joints = {j["nodes"][idx].get("name") for skin in j.get("skins", [])
              for idx in skin.get("joints", [])}
    return low, high, joints

spider_low, spider_high, spider_joints = inspect(sys.argv[1])
npc_low, npc_high, npc_joints = inspect(sys.argv[2])

# Without the bind matrices the spider Y bounds are -4.95..4.61 and its legs
# fold through the body. Correct bind-pose geometry is approximately
# -0.65..6.07 and has a real nested joint hierarchy.
assert spider_low[1] > -1.0 and spider_high[1] > 6.0
assert {"ins_taran", "root", "legL0", "legR0"} <= spider_joints

# The broken NPC is a sideways -9.7..9.7 blob. The corrected character is
# upright, about 37 units tall, and symmetric across X.
assert npc_low[0] < -12.0 and npc_high[0] > 12.0
assert npc_low[1] > -1.0 and npc_high[1] > 35.0
assert {"root", "base", "head", "Lfoot1", "Rfoot1", "tail1"} <= npc_joints
PY
  then
    ok "ACCF MDL0 bind-pose transforms -> upright spider + NPC"
  else
    no "ACCF MDL0 bind-pose transforms" "$out"
  fi
  rm -rf "$out"
}
t_accf_bind_pose

t_accf_node_mix(){
  # A separate small ACCF model whose GX stream uses three genuine NodeMix
  # influences (up to three bones per influence). At bind pose those matrix
  # nodes must evaluate through bind*inverseBind, while its primary nodes
  # still receive their bone matrices.
  local src="$PWD_PROJECT/../tests/fixtures/accf_ins_mukade.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 NodeMix transforms"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_nodemix.XXXXXX) || { no "ACCF MDL0 NodeMix transforms" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out/model" --overwrite >/dev/null 2>&1
  local glb="$out/model/3DModels(NW4R)/ins_mukade.glb"
  if python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
j = g.json
points = []
for mesh in j.get("meshes", []):
    for prim in mesh.get("primitives", []):
        pos_idx = prim.get("attributes", {}).get("POSITION")
        if pos_idx is not None:
            points.extend(g.read_accessor_tuples(pos_idx))
low = [min(p[i] for p in points) for i in range(3)]
high = [max(p[i] for p in points) for i in range(3)]
joints = {j["nodes"][idx].get("name") for skin in j.get("skins", [])
          for idx in skin.get("joints", [])}
# The old all-raw path displaced the left side to X=-5.52. Correct primary
# and mixed node handling restores the symmetric -3.32..3.32 centipede.
assert -3.5 < low[0] < -3.0 and 3.0 < high[0] < 3.5
assert abs(low[0] + high[0]) < 0.05
assert {"a", "b", "c", "d", "e"} <= joints
PY
  then
    ok "ACCF MDL0 NodeMix transforms -> symmetric centipede"
  else
    no "ACCF MDL0 NodeMix transforms" "$out"
  fi
  rm -rf "$out"
}
t_accf_node_mix

t_accf_palette_corpus(){
  # These small retail samples cover the palette relationships that cannot be
  # represented by a single same-name fixture: dotted PAT0 frames, localized
  # shared palettes, MDL0 texture/palette resource links, and a runtime TLUT
  # whose on-disc palette initializes fewer colors than its CI8 indices use.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local specs="accf_Ftr156.brres:7 accf_Ftr368.brres:6 accf_fgItem.brres:111 accf_npc_special_13.brres:10 accf_bgmodel_193.brres:10"
  local ran=0
  for spec in $specs; do
    local name=${spec%:*} expected=${spec#*:} src="$sample_dir/${spec%:*}"
    [ -f "$src" ] || continue
    ran=1
    local out
    out=$(mktemp -d /tmp/_r_accf_palette.XXXXXX) || { no "ACCF palette corpus: $name" "mktemp failed"; continue; }
    $B/wszst XDECODE "$src" --dest "$out" --overwrite >"$out.log" 2>&1
    local count
    count=$(find "$out/Textures(NW4R)" -type f -name '*.png' 2>/dev/null | wc -l | tr -d ' ')
    if [ "$count" -eq "$expected" ] \
        && ! grep -q 'INVALID IMAGE FORMAT\|PNG ERROR' "$out.log"; then
      ok "ACCF palette corpus: $name -> $count PNGs"
    else
      no "ACCF palette corpus: $name" "expected $expected PNGs, got $count"
    fi
  done
  [ "$ran" -eq 1 ] || sk "ACCF palette corpus"
}
t_accf_palette_corpus

t_accf_shared_texture_tree(){
  # Terrain MDL0s and their seasonal TEX0s are in different BRRES archives.
  # Directory-mode XX must finish decoding the tree, index its PNGs, and only
  # then export DAEs so init_from is neither dangling nor traversal-order based.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local model="$sample_dir/accf_000_0.brres"
  local pack="$sample_dir/accf_pat0season00.brres"
  if [ ! -f "$model" ] || [ ! -f "$pack" ]; then sk "ACCF shared BRRES texture tree"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_shared.XXXXXX) || { no "ACCF shared BRRES texture tree" "mktemp failed"; return; }
  mkdir -p "$tree/BgData/BgModel" "$tree/BgData/Pack"
  cp "$model" "$tree/BgData/BgModel/"
  cp "$pack" "$tree/BgData/Pack/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local glb="$tree/BgData/BgModel/accf_000_0.brres.d/3DModels(NW4R)/grd_Ce_0.glb"
  local grass="$tree/BgData/Pack/accf_pat0season00.brres.d/Textures(NW4R)/tex_grass.png"
  # The cross-archive case is exactly where a self-contained model matters
  # most: the resolved image lives several directories away, so GLB embeds
  # it directly in the binary rather than depending on a relative init_from.
  if [ -s "$glb" ] && [ -s "$grass" ] \
      && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1 \
      && python3 - "$glb" "$grass" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
grass_bytes = open(sys.argv[2], "rb").read()
images = g.json.get("images", [])
assert images, "no images embedded"
found = any(g.image_bytes(i) == grass_bytes for i in range(len(images))
            if g.image_bytes(i) is not None)
assert found, "grass texture bytes not embedded in GLB"
PY
  then
    ok "ACCF shared BRRES texture tree -> resolved GLB image"
  else
    no "ACCF shared BRRES texture tree" "$tree"
  fi
}
t_accf_shared_texture_tree

t_accf_v9_texture_links(){
  # v8/v9 MDL0 material-ref strings can point into the BRRES-wide pool. The
  # detached MDL0 compatibility pool does not rewrite those particular
  # fields, so recover the exact layer mapping through the MDL0 Textures
  # resource group (the same material-ref linkage BrawlCrate uses). Include a
  # nearby NPC archive full of generic e.0/m.0 names to prove global basename
  # lookup does not attach unrelated eye/mouth textures to this model.
  local sample_dir="$PWD_PROJECT/../tests/fixtures"
  local model="$sample_dir/accf_excap0.brres"
  local npc="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$model" ] || [ ! -f "$npc" ]; then sk "ACCF v9 material texture links"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_v9.XXXXXX) || { no "ACCF v9 material texture links" "mktemp failed"; return; }
  mkdir -p "$tree/Item/Excap" "$tree/Npc/Special"
  cp "$model" "$tree/Item/Excap/"; cp "$npc" "$tree/Npc/Special/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local glb="$tree/Item/Excap/accf_excap0.brres.d/3DModels(NW4R)/excap998.glb"
  local glb_report
  glb_report=$(python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
try:
    g = m.load_glb(sys.argv[1])
except Exception as exc:
    print(f"PARSE_FAIL {exc}"); sys.exit(0)
j = g.json
img_names = [im.get("name") for im in j.get("images", [])]
mat_names = [mt.get("name") for mt in j.get("materials", [])]
mesh_names = [me.get("name") for me in j.get("meshes", [])]
node_names = [n.get("name") for n in j.get("nodes", [])]
print(f"n_images={len(j.get('images', []))}")
print(f"n_materials={len(j.get('materials', []))}")
print(f"n_meshes={len(j.get('meshes', []))}")
print(f"has_excap998_0={'excap998_0' in mesh_names or 'excap998_0' in node_names or 'excap998_0' in img_names}")
print(f"has_h={'h' in mat_names}")
print(f"leaked_generic={any(n in ('e.0', 'm.0') for n in img_names + mat_names)}")
PY
)
  if echo "$glb_report" | grep -q "n_images=2" \
      && echo "$glb_report" | grep -q "n_materials=4" \
      && echo "$glb_report" | grep -q "n_meshes=4" \
      && echo "$glb_report" | grep -q "has_excap998_0=True" \
      && echo "$glb_report" | grep -q "has_h=True" \
      && echo "$glb_report" | grep -q "leaked_generic=False" \
      && python3 ../tests/validate-glb.py --require-images "$glb" >/dev/null 2>&1; then
    ok "ACCF v9 material links -> 4 named materials, 2 correct local textures"
  else
    no "ACCF v9 material texture links" "$tree"
  fi
  rm -rf "$tree"
}
t_accf_v9_texture_links

t_accf_bone_only_dae(){
  # Retail scene placeholders can contain a real bone hierarchy and no GX
  # objects. BrawlCrate exports these as skeleton-only COLLADA; XX used to
  # silently drop them because ParseMDL0 required at least one mesh.
  local src="$PWD_PROJECT/../tests/fixtures/accf_bone_only_229.brres"
  [ -f "$src" ] || { sk "ACCF bone-only MDL0 -> DAE"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_empty.XXXXXX) || { no "ACCF bone-only MDL0 -> DAE" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/idr_ms_pictureA2.glb"
  if [ -s "$glb" ] && python3 - "$glb" <<'PY'
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
j = g.json
names = [n.get("name") for n in j.get("nodes", [])]
assert "idr_ms_pictureA2" in names, f"root joint node missing: {names}"
assert not j.get("meshes"), f"bone-only model unexpectedly has meshes: {j.get('meshes')}"
PY
  then
    ok "ACCF bone-only MDL0 -> skeleton GLB"
  else
    no "ACCF bone-only MDL0 -> DAE" "$src"
  fi
  rm -rf "$out"
}
t_accf_bone_only_dae

t_accf_skeleton_bind_pose(){
  # The COLLADA skeleton and the skin's inverse bind matrices have to agree:
  # walking the joint tree and multiplying each joint's world matrix by its
  # exported inverse bind matrix must give identity, or importers deform the
  # mesh the moment they bind it. This catches two real regressions at once --
  # writing the joint rotations in X,Y,Z order (NW4R composes T*Rz*Ry*Rx*S),
  # and treating a segment-scale-compensate bone as an ordinary TRS node.
  local src="$HOME/Downloads/Animal Crossing City Folk Deluxe [RUUE02].d/files/Item/OrgUmb/OrgUmb25.brres"
  [ -f "$src" ] || { sk "ACCF skeleton/skin bind pose"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_skin.XXXXXX) || { no "ACCF skeleton/skin bind pose" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/umb_md.glb"
  # This catches two real regressions at once -- writing the joint rotations
  # in X,Y,Z composition order (NW4R composes T*Rz*Ry*Rx*S), and treating a
  # segment-scale-compensate bone as an ordinary TRS node. The GLB exporter
  # bakes each joint's local transform straight into its node "matrix" (see
  # dae_joint_local_matrix() in lib-model-dae.c, shared with the DAE path),
  # so this is the same invariant, just walked via glTF nodes/skins instead
  # of COLLADA's <node>/<matrix> tree.
  if [ -s "$glb" ] && python3 - "$glb" <<'SKINPY' >/dev/null 2>&1
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)

g = m.load_glb(sys.argv[1])
j = g.json

controllers = 0
for skin in j.get("skins", []):
    joint_indices = skin["joints"]
    ibm = g.read_accessor_tuples(skin["inverseBindMatrices"])
    assert len(ibm) == len(joint_indices), "inverse bind matrix count"
    cache = {}
    for node_idx, inv_flat in zip(joint_indices, ibm):
        world = m.node_world_matrix(g, node_idx, cache)
        inverse = list(inv_flat)
        product = m.mat4_mul(world, inverse)
        expected = m.mat4_identity()
        for k in range(16):
            assert abs(product[k] - expected[k]) < 2e-3, (j["nodes"][node_idx].get("name"), k, product[k])
    controllers += 1

assert controllers > 0, "no skin controllers"
SKINPY
  then
    ok "ACCF skeleton/skin bind pose -> world(joint) * invBind == identity"
  else
    no "ACCF skeleton/skin bind pose" "$src"
  fi
  rm -rf "$out"
}
t_accf_skeleton_bind_pose

t_accf_face_winding(){
  # GX display lists store each primitive'"'"'s vertices in the order the hardware
  # consumes them, which is the reverse of COLLADA'"'"'s front-facing winding.
  # Getting this wrong is invisible in a viewer that does not cull backfaces
  # (and in assimp'"'"'s importer), and turns the model inside out in one that
  # does -- Preview/SceneKit, Unity, Unreal. The check is implementation
  # independent: a triangle'"'"'s geometric normal (right-hand rule) must agree
  # in sign with the artist-authored per-vertex normals stored in the MDL0.
  local src="$PWD_PROJECT/../tests/fixtures/accf_ins_taran.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 face winding"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_wind.XXXXXX) || { no "ACCF MDL0 face winding" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local glb="$out/3DModels(NW4R)/ins_taran.glb"
  # glTF's indices/POSITION/NORMAL are already per-corner (no COLLADA-style
  # separate offset/stride input tuples to unpack), so this is simpler than
  # the old DAE version but checks the identical invariant.
  if [ -s "$glb" ] && python3 - "$glb" <<'WINDPY' >/dev/null 2>&1
import sys
sys.path.insert(0, "../tests")
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


g = m.load_glb(sys.argv[1])
agree = disagree = 0
for mesh in g.json.get("meshes", []):
    for prim in mesh.get("primitives", []):
        attrs = prim.get("attributes", {})
        if "NORMAL" not in attrs or "indices" not in prim:
            continue
        positions = g.read_accessor_tuples(attrs["POSITION"])
        normals = g.read_accessor_tuples(attrs["NORMAL"])
        indices = g.read_accessor(prim["indices"])
        for base in range(0, len(indices) - 2, 3):
            corner = indices[base:base + 3]
            p0, p1, p2 = (positions[i] for i in corner)
            e1 = [p1[i] - p0[i] for i in range(3)]
            e2 = [p2[i] - p0[i] for i in range(3)]
            geometric = cross(e1, e2)
            authored = [sum(normals[i][axis] for i in corner) / 3.0 for axis in range(3)]
            dot = sum(geometric[i] * authored[i] for i in range(3))
            magnitude = sum(v * v for v in geometric) ** 0.5
            if magnitude < 1e-9:
                continue
            if dot > 0:
                agree += 1
            elif dot < 0:
                disagree += 1

assert agree + disagree > 100, "not enough triangles to judge"
ratio = agree / float(agree + disagree)
assert ratio > 0.9, "faces wound backwards: only %.1f%% agree with normals" % (ratio * 100)
WINDPY
  then
    ok "ACCF MDL0 face winding -> front faces agree with authored normals"
  else
    no "ACCF MDL0 face winding" "$src"
  fi
  rm -rf "$out"
}
t_accf_face_winding

t_dae_brres_injection(){
  # Test DAE -> BRRES injection (with parent BRRES and parent MDL0)
  local sample="/Volumes/SSD/shiran/NintendoWare 2/Revolution/Viewer/build/demos/ef_g3d/data/butterfly.brres"
  [ -f "$sample" ] || sample=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 5 -name "butterfly.brres" -print -quit 2>/dev/null; done | head -1)
  [ -n "$sample" ] && [ -f "$sample" ] || { sk "DAE -> BRRES injection"; return; }

  local out
  out=$(mktemp -d /tmp/_r_dae_inject.XXXXXX) || { no "DAE -> BRRES injection" "mktemp failed"; return; }

  # 1. Extract sample
  $B/wszst extract "$sample" -d "$out/orig.d" >/dev/null 2>&1
  local mdl="$out/orig.d/3DModels(NW4R)/butterfly"
  [ -f "$mdl" ] || { no "DAE -> BRRES injection" "failed to extract butterfly mdl0"; rm -rf "$out"; return; }

  # 2. Decode to DAE
  $B/wmdlt decode "$mdl" -d "$out/butterfly.dae" >/dev/null 2>&1
  [ -s "$out/butterfly.dae" ] || { no "DAE -> BRRES injection" "failed to decode butterfly to DAE"; rm -rf "$out"; return; }

  # 3. Inject DAE into parent BRRES
  $B/wmdlt encode "$out/butterfly.dae" --parent="$sample" -d "$out/injected.brres" --overwrite >/dev/null 2>&1
  [ -s "$out/injected.brres" ] || { no "DAE -> BRRES injection" "failed to inject DAE into parent BRRES"; rm -rf "$out"; return; }

  # 4. Extract injected BRRES and verify NW4R directory layout
  $B/wszst extract "$out/injected.brres" -d "$out/reextract.d" >/dev/null 2>&1
  local re_mdl="$out/reextract.d/3DModels(NW4R)/butterfly"
  local re_tex="$out/reextract.d/Textures(NW4R)/oumrasaki"
  if [ -f "$re_mdl" ] && [ -f "$re_tex" ]; then
    ok "DAE -> BRRES injection (with parent BRRES and folder hierarchy)"
  else
    no "DAE -> BRRES injection" "missing subfiles after re-extracting injected BRRES"
  fi

  # 5. Direct MDL0 injection test
  $B/wmdlt encode "$out/butterfly.dae" --parent="$mdl" -d "$out/injected.mdl0" --overwrite >/dev/null 2>&1
  if [ -s "$out/injected.mdl0" ]; then
    ok "DAE -> MDL0 injection (with parent MDL0)"
  else
    no "DAE -> MDL0 injection" "failed to inject DAE directly into MDL0"
  fi

  rm -rf "$out"
}
t_dae_brres_injection

t_dae_multiformat_injection(){
  # Test DAE -> BCH injection
  local bch_sample="/Users/larsen/Downloads/aaaaa/live1/h3d/Mii_body.bch"
  [ -f "$bch_sample" ] || bch_sample=$(for d in $SEARCH; do [ -d "$d" ] || continue; find -L "$d" -maxdepth 6 -name "*.bch" -print -quit 2>/dev/null; done | head -1)
  if [ -n "$bch_sample" ] && [ -f "$bch_sample" ]; then
    local out; out=$(mktemp -d /tmp/_r_dae_bch.XXXXXX) || { no "DAE -> BCH injection" "mktemp failed"; return; }
    $B/wmdlt ENCODE "$bch_sample" -d "$out/orig.dae" --overwrite >/dev/null 2>&1
    if [ -s "$out/orig.dae" ]; then
      $B/wmdlt ENCODE "$out/orig.dae" --parent="$bch_sample" -d "$out/injected.bch" --overwrite >/dev/null 2>&1
      if [ -s "$out/injected.bch" ]; then
        $B/wmdlt ENCODE "$out/injected.bch" -d "$out/redecoded.dae" --overwrite >/dev/null 2>&1
        local g; g=$(grep -c '<geometry' "$out/redecoded.dae" 2>/dev/null || echo 0)
        if [ "$g" -gt 0 ]; then
          ok "DAE -> BCH injection (with parent BCH: $g geometries)"
        else
          no "DAE -> BCH injection" "failed to re-decode injected BCH"
        fi
      else
        no "DAE -> BCH injection" "failed to write injected.bch"
      fi
    else
      no "DAE -> BCH injection" "failed to decode initial BCH to DAE"
    fi
    rm -rf "$out"
  else
    sk "DAE -> BCH injection"
  fi
}
t_dae_multiformat_injection

t_accf_breft_indexed(){
  # REFT/BREFT entries keep their palette inline after the image payload.
  # This retail ACCF C4 entry is 8x16 with a 16-color RGB5A3 palette; the old
  # placeholder header treated those fields as unknown and passed PAL_INVALID.
  local src="$PWD_PROJECT/../tests/fixtures/accf_breft_indexed.bt-img"
  [ -f "$src" ] || { sk "ACCF indexed BREFT texture"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_breft.XXXXXX) || { no "ACCF indexed BREFT texture" "mktemp failed"; return; }
  $B/wimgt DECODE "$src" -d "$out/flower.png" --overwrite >"$out/decode.log" 2>&1
  if [ -s "$out/flower.png" ] \
      && grep -q 'BREFT-IMG.C4.PRGB5A3' "$out/decode.log" \
      && python3 - "$out/flower.png" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read(24)
assert data[:8] == b'\x89PNG\r\n\x1a\n'
assert struct.unpack('>II', data[16:24]) == (8, 16)
PY
  then
    ok "ACCF indexed BREFT texture -> inline RGB5A3 palette"
  else
    no "ACCF indexed BREFT texture" "$src"
  fi
  rm -rf "$out"
}
t_accf_breft_indexed

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
  local f; f=$(find_magic "RFNT"); [ -n "$f" ] || { sk "BRFNT (Wii bitmap font)"; return; }
  rm -f /tmp/_r_brfnt.png /tmp/_r_brfnt.img000.png
  $B/wimgt DECODE "$f" -d /tmp/_r_brfnt.png --overwrite >/dev/null 2>&1
  [ -s /tmp/_r_brfnt.img000.png ] && ok "BRFNT (Wii bitmap font) -> PNG ($f)" \
    || no "BRFNT (Wii bitmap font)" "$f"
}
t_brfnt

t_brfna(){
  # BRFNA (Wii "archived" bitmap font, magic "RFNA"): same RFNT-family
  # container/TGLP shape as .brfnt (confirmed by static RE of
  # nw4r_fontcvtr.exe -- see brfna_archived_font_format memory). Every real
  # sample sets TGLP sheetFormat's bit 0x8000, meaning the sheet pixel data
  # isn't raw GX texture data at all -- it's compressed with a proprietary,
  # undocumented codec (three opcodes: LZSS, RLE, and a self-contained
  # canonical-Huffman bit-walk), decompiled from nw4r_fontcvtr.exe via Ghidra
  # and implemented natively in lib-image2.c's DecodeBRFNA_LZSS/RLE/Huffman +
  # DecompressBRFNASheet. Declared sheet counts also routinely exceed what's
  # physically embedded (e.g. RVL_SDK wbf1.brfna declares 70, and now that
  # decompression works all 70 really are present as separate compressed
  # chunks -- an earlier byte-budget clamp for the *uncompressed* case was a
  # red herring caused by not yet knowing the sheets were compressed at all).
  #
  # Verified for real pixel correctness, not just "a file got created": every
  # sheet checked from RVL_SDK fonts/fonts_chn/fonts_kor and the NintendoWare
  # LayoutEditor test_sample/font/*.brfna corpus renders as actual legible
  # glyphs (Latin/symbol, Japanese kana+kanji, and Simplified Chinese sheets
  # all visually confirmed). This test can't render images to eyeball, so it
  # uses PNG file size as a real-content proxy instead of just existence: a
  # genuinely blank/degenerate 32x1024 grayscale sheet PNG-compresses to
  # ~100 bytes, while every real decoded glyph sheet checked was several KB+.
  local f; f=$(find_magic "RFNA"); [ -n "$f" ] || { sk "BRFNA (Wii archived font)"; return; }
  rm -rf /tmp/_r_brfna; mkdir -p /tmp/_r_brfna
  $B/wszst XX "$f" --dest /tmp/_r_brfna/out --overwrite >/dev/null 2>&1
  local pngs; pngs=$(find /tmp/_r_brfna -name '*.png' -o -name 'out*' -type f 2>/dev/null)
  local n; n=$(printf '%s\n' "$pngs" | grep -c .)
  local small; small=$(for p in $pngs; do [ "$(stat -f%z "$p" 2>/dev/null||echo 0)" -lt 500 ] && echo "$p"; done | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && [ "$small" -eq 0 ]; then
    ok "BRFNA (Wii archived font) -> $n real, non-blank sheet PNG(s) ($f)"
  else
    no "BRFNA (Wii archived font)" "$f ($small/$n sheet(s) suspiciously small/blank)"
  fi
}
t_brfna

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

t_nccarc(){
  # NCCARC has no magic (see the long comment above ScanNCCARC() in
  # lib-nintendo.c), so it can't go through the magic-keyed IDX -- found by
  # extension instead, straight from SEARCH.
  local f=""
  for d in $SEARCH; do [ -d "$d" ] || continue
    f=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.nccarc' 2>/dev/null | head -1)
    [ -n "$f" ] && break
  done
  [ -n "$f" ] || { sk "NCCARC (WarioWare: Touched!)"; return; }
  rm -rf /tmp/_r_nccarc; mkdir -p /tmp/_r_nccarc
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_nccarc/\1N" --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_nccarc -type f 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "NCCARC (WarioWare: Touched!) -> $n chunk(s) ($f)" || no "NCCARC (WarioWare: Touched!)" "$f"
}
t_nccarc

t_gfa(){
  # GFAC (Good-Feel archive); GFCP zip-mode 1 payloads are BPE-compressed.
  # The BPE decoder desynced on every real retail sample until it was fixed
  # against a live Kirby's Epic Yarn WBFS -- assert real decoded output
  # here, not just "some file got created", so a regression shows up as a
  # missing/empty member rather than a silent pass.
  # Prefer the committed fixture (extracted with `wit EXTRACT -F +...bean00.gfa
  # -F -*` straight out of a real, already-on-disk Kirby's Epic Yarn (USA)
  # WBFS, no synthesis involved) so this doesn't depend on an external
  # dump being present in $SEARCH; fall back to the dynamic scan otherwise.
  local f="$PWD_PROJECT/../tests/fixtures/gfa_bean00.gfa"
  [ -f "$f" ] || f=$(find_magic "GFAC")
  [ -n "$f" ] || { sk "GFA (Good-Feel archive)"; return; }
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

t_warc(){
  # WARC ("WARC" magic): Game & Wario (Wii U) flat archive, big-endian,
  # uncompressed, unrelated to Excite's TOC/RES despite the naming
  # coincidence. Ported from aluigi's game_wario.bms.
  local f; f=$(find_magic "WARC"); [ -n "$f" ] || { sk "WARC (Game & Wario archive)"; return; }
  rm -rf /tmp/_r_warc; mkdir -p /tmp/_r_warc
  $B/wszst EXTRACT "$f" --dest "/tmp/_r_warc/\1N" --overwrite >/tmp/_r_warc.log 2>&1
  local n; n=$(find /tmp/_r_warc -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n" -gt 0 ] && ! grep -q "INVALID" /tmp/_r_warc.log; then
    ok "WARC (Game & Wario archive) -> $n non-empty member(s) ($f)"
  else
    no "WARC (Game & Wario archive)" "$f"
  fi
}
t_warc

t_ctpk(){
  # CTPK (CTR Texture Package, 3DS container): contains one or more 3DS GPU textures
  # (Morton tiled, ETC1/ETC1A4/RGBA8/RGB565/etc.). Native extraction via wszst xx
  # decodes each sub-texture directly to PNG.
  local f; f=$(find_magic "CTPK"); [ -n "$f" ] || { sk "CTPK (3DS texture container)"; return; }
  rm -rf /tmp/_r_ctpk; mkdir -p /tmp/_r_ctpk
  $B/wszst EXTRACT "$f" --dest /tmp/_r_ctpk --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_ctpk -type f -iname '*.png' -size +0c 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "CTPK (3DS texture container) -> $n PNG texture(s) ($f)" || no "CTPK (3DS texture container)" "$f"
}
t_ctpk

t_ctpk_img(){
  local f; f=$(find_magic "CTPK"); [ -n "$f" ] || return
  rm -f /tmp/_r_ctpk_img*.png
  $B/wimgt DECODE "$f" -d /tmp/_r_ctpk_img.png --overwrite >/dev/null 2>&1
  local n; n=$(ls /tmp/_r_ctpk_img*.png 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "CTPK direct decode (wimgt) -> $n PNG(s)" || no "CTPK direct decode (wimgt)" "$f"
}
t_ctpk_img

t_byml(){
  # BYML (Binary YAML parameter format, 3DS / Wii U / Switch):
  # Decodes to valid human-readable YAML and re-encodes back to BYML.
  local f; f=$(find_magic "YB"); [ -n "$f" ] || f=$(find_magic "BY"); [ -n "$f" ] || { sk "BYML parameter decode"; return; }
  rm -f /tmp/_r_byml.yaml /tmp/_r_byml_re.byml /tmp/_r_byml_re.yaml
  $B/wszst TEXT "$f" --dest /tmp/_r_byml.yaml --overwrite >/dev/null 2>&1
  if [ -s /tmp/_r_byml.yaml ] \
  && "$B/wszst" CREATE /tmp/_r_byml.yaml --dest /tmp/_r_byml_re.byml --overwrite >/dev/null 2>&1 \
  && "$B/wszst" TEXT /tmp/_r_byml_re.byml --dest /tmp/_r_byml_re.yaml --overwrite >/dev/null 2>&1 \
  && cmp -s /tmp/_r_byml.yaml /tmp/_r_byml_re.yaml; then
    ok "BYML decode -> encode roundtrip ($f)"
  elif [ -s /tmp/_r_byml.yaml ] && python3 -c "import yaml; d = yaml.safe_load(open('/tmp/_r_byml.yaml')); assert len(d) > 0" 2>/dev/null; then
    ok "BYML -> valid YAML ($f)"
  else
    no "BYML parameter decode" "$f"
  fi
}
t_byml

t_narc(){
  # NARC (Nitro Archive, DS / 3DS):
  # Native extraction unwraps all member files with full directory tree.
  local f; f=$(find_magic "NARC"); [ -n "$f" ] || { sk "NARC (Nitro Archive)"; return; }
  rm -rf /tmp/_r_narc; mkdir -p /tmp/_r_narc
  $B/wszst EXTRACT "$f" --dest /tmp/_r_narc --overwrite >/dev/null 2>&1
  local n; n=$(find /tmp/_r_narc -type f -size +0c 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" -gt 0 ] && ok "NARC (Nitro Archive) -> $n non-empty member(s) ($f)" || no "NARC (Nitro Archive)" "$f"
}
t_narc

echo "== BFWAV/BCWAV (FWAV/CWAV NintendoWare wave audio -> PCM WAV) =="
t_bxwav(){
  # FWAV (Wii U/Switch) and CWAV (3DS) share the RWAV lineage but aren't
  # indexed by magic here (they only ever turn up inside a BFWAR/BCWAR
  # sound-wave-archive, never as a standalone top-level sample), so find one
  # via extension straight from SEARCH, same as t_nccarc.
  local arc=""
  for d in $SEARCH; do [ -d "$d" ] || continue
    arc=$(find -L "$d" -maxdepth 8 -type f -size +1000c \( -iname '*.bfsar' -o -iname '*.bcsar' \) 2>/dev/null | head -1)
    [ -n "$arc" ] && break
  done
  [ -n "$arc" ] || { sk "BFWAV/BCWAV -> WAV"; return; }

  rm -rf /tmp/_r_bxwav; mkdir -p /tmp/_r_bxwav
  $B/wszst xx "$arc" --dest /tmp/_r_bxwav --overwrite >/dev/null 2>&1

  local n_wav; n_wav=$(find /tmp/_r_bxwav -iname '*.wav' -size +44c 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n_wav" -eq 0 ]; then
    no "BFWAV/BCWAV -> WAV" "no WAV produced from $arc"
    return
  fi

  # A broken decode still emits a correctly-sized RIFF/WAVE header (silence
  # or a flat DC offset), so check for actual sample variance -- same
  # standard as t_bntx_astc's colour-count check -- on one real sample.
  local sample; sample=$(find /tmp/_r_bxwav -iname '*.wav' -size +200c 2>/dev/null | head -1)
  local nonzero_variety
  nonzero_variety=$(python3 -c "
import wave
try:
    w = wave.open('$sample','rb')
    import struct
    data = w.readframes(w.getnframes())
    n = w.getsampwidth()
    if n == 2:
        samples = struct.unpack('<%dh' % (len(data)//2), data)
    else:
        samples = data
    print(len(set(samples)))
except Exception:
    print(0)
" 2>/dev/null)
  if [ "${nonzero_variety:-0}" -gt 10 ]; then
    ok "BFWAV/BCWAV -> WAV ($n_wav file(s), $arc)"
  else
    no "BFWAV/BCWAV -> WAV" "decoded but looks silent/flat: $sample"
  fi
}
t_bxwav

echo "== compression round-trips =="
# The compression format is chosen by the DESTINATION EXTENSION, not a flag.
printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > /tmp/_r.bin
for e in lz10 lz11 rl yay0 ash0 lzh8 qlz at7 blz huff4 huff8 stpl rnc zlib deflate; do
  rm -f /tmp/_r.$e /tmp/_r.out
  if $B/wszst COMPRESS /tmp/_r.bin --dest /tmp/_r.$e --overwrite >/dev/null 2>&1 \
  && $B/wszst DECOMPRESS /tmp/_r.$e --dest /tmp/_r.out --overwrite >/dev/null 2>&1 \
  && cmp -s /tmp/_r.bin /tmp/_r.out; then
    ok "$e round-trip ($(stat -f%z /tmp/_r.$e 2>/dev/null||echo ?) B)"
  else no "$e round-trip" "mismatch"; fi
done

echo "== container creation round-trips =="
t_container_roundtrips(){
  local d; d=$(mktemp -d)
  mkdir -p "$d/tree/sub"
  printf 'File 1 payload data in root directory\n' > "$d/tree/file1.bin"
  printf 'File 2 payload data in sub directory\n' > "$d/tree/sub/file2.bin"

  # NARC
  rm -rf "$d/narc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.narc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.narc" --dest "$d/narc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/narc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/narc.out/sub/file2.bin"; then
    ok "NARC create -> extract roundtrip"
  else
    no "NARC create -> extract" "mismatch"
  fi

  # DARC
  rm -rf "$d/darc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.darc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.darc" --dest "$d/darc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/darc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/darc.out/sub/file2.bin"; then
    ok "DARC create -> extract roundtrip"
  else
    no "DARC create -> extract" "mismatch"
  fi

  # PAC
  rm -rf "$d/pac.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.pac" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.pac" --dest "$d/pac.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/pac.out/0000_00.MiscData.bin" ] && [ -s "$d/pac.out/0001_00.MiscData.bin" ]; then
    ok "PAC create -> extract roundtrip"
  else
    no "PAC create -> extract" "mismatch"
  fi

  # GFA
  rm -rf "$d/gfa.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.gfa" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.gfa" --dest "$d/gfa.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/gfa.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/gfa.out/sub/file2.bin"; then
    ok "GFA create -> extract roundtrip"
  else
    no "GFA create -> extract" "mismatch"
  fi

  # RARC
  rm -rf "$d/rarc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.rarc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.rarc" --dest "$d/rarc.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/rarc.out/file1.bin" \
  && cmp -s "$d/tree/sub/file2.bin" "$d/rarc.out/sub/file2.bin"; then
    ok "RARC create -> extract roundtrip"
  else
    no "RARC create -> extract" "mismatch"
  fi

  # WUX (Wii U sparse disc compression)
  if "$B/wszst" COMPRESS "$d/tree/file1.bin" --dest "$d/test.wux" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" DECOMPRESS "$d/test.wux" --dest "$d/test.wud" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/test.wud"; then
    ok "WUX compress -> decompress roundtrip"
  else
    no "WUX compress -> decompress" "mismatch"
  fi

  # CCF (Virtual Console archive)
  rm -rf "$d/ccf.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.ccf" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.ccf" --dest "$d/ccf.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/ccf.out/file1.bin" ] && [ -s "$d/ccf.out/file2.bin" ]; then
    ok "CCF create -> extract roundtrip"
  else
    no "CCF create -> extract" "mismatch"
  fi

  # NCCARC (WarioWare blob container)
  rm -rf "$d/nccarc.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.nccarc" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.nccarc" --dest "$d/nccarc.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/nccarc.out/0000.bin" ] && [ -s "$d/nccarc.out/0001.bin" ]; then
    ok "NCCARC create -> extract roundtrip"
  else
    no "NCCARC create -> extract" "mismatch"
  fi

  # AT7 (PMD archive)
  rm -rf "$d/at7.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.at7" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.at7" --dest "$d/at7.out" --overwrite >/dev/null 2>&1 \
  && cmp -s "$d/tree/file1.bin" "$d/at7.out/file1.bin"; then
    ok "AT7 create -> extract roundtrip"
  else
    no "AT7 create -> extract" "mismatch"
  fi

  # MPBIN (Mario Party .bin container)
  rm -rf "$d/mpbin.out"
  if "$B/wszst" CREATE "$d/tree" --dest "$d/test.bin" --overwrite >/dev/null 2>&1 \
  && "$B/wszst" EXTRACT "$d/test.bin" --dest "$d/mpbin.out" --overwrite >/dev/null 2>&1 \
  && [ -s "$d/mpbin.out/file000.dat" ] && [ -s "$d/mpbin.out/file001.dat" ]; then
    ok "MPBIN create -> extract roundtrip"
  else
    no "MPBIN create -> extract" "mismatch"
  fi

  # CTPK, NCGR, NCLR
  if command -v python3 >/dev/null; then
    python3 -c "
from PIL import Image
im = Image.new('RGBA', (32, 32), color=(100, 150, 200, 255))
im.save('$d/img.png')
im_ncgr = Image.new('RGBA', (64, 64), color=(128, 128, 128, 255))
im_ncgr.save('$d/ncgr_in.png')
im_nclr = Image.new('RGBA', (128, 128), color=(255, 0, 0, 255))
im_nclr.save('$d/nclr_in.png')
" 2>/dev/null
    rm -rf "$d/ctpk.out"
    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.ctpk" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.ctpk" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/test.ctpk.d/img.png" ]; then
      ok "CTPK encode -> extract roundtrip"
    else
      no "CTPK encode -> extract" "mismatch"
    fi

    # CTPK folder creation (wszst CREATE)
    mkdir -p "$d/ctpk_dir"
    cp "$d/img.png" "$d/ctpk_dir/tex_a.png"
    cp "$d/ncgr_in.png" "$d/ctpk_dir/tex_b.png"
    rm -rf "$d/ctpk_multi.out"
    if "$B/wszst" CREATE "$d/ctpk_dir" --dest "$d/multi.ctpk" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/multi.ctpk" --dest "$d/ctpk_multi.out" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/ctpk_multi.out/tex_a.png" ] && [ -s "$d/ctpk_multi.out/tex_b.png" ]; then
      ok "CTPK folder create -> extract roundtrip"
    else
      no "CTPK folder create -> extract" "mismatch"
    fi

    if [ -f "$d/ncgr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/ncgr_in.png" --dest "$d/test.ncgr" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.ncgr" --dest "$d/ncgr_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/ncgr_out.png" ]; then
      ok "NCGR encode -> decode roundtrip"
    else
      no "NCGR encode -> decode" "mismatch"
    fi

    if [ -f "$d/nclr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/nclr_in.png" --dest "$d/test.nclr" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.nclr" --dest "$d/nclr_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/nclr_out.png" ]; then
      ok "NCLR encode -> decode roundtrip"
    else
      no "NCLR encode -> decode" "mismatch"
    fi

    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.bntx" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.bntx" --dest "$d/bntx_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/bntx_out.png" ]; then
      ok "BNTX encode -> decode roundtrip"
    else
      no "BNTX encode -> decode" "mismatch"
    fi

    if [ -f "$d/nclr_in.png" ] \
    && "$B/wimgt" ENCODE "$d/nclr_in.png" --dest "$d/test.plt0" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.plt0" --dest "$d/plt0_out.png" --overwrite >/dev/null 2>&1 \
    && [ -f "$d/plt0_out.png" ]; then
      ok "PLT0 encode -> decode roundtrip"
    else
      no "PLT0 encode -> decode" "mismatch"
    fi

    if [ -f "$d/img.png" ] \
    && "$B/wimgt" ENCODE "$d/img.png" --dest "$d/test.tex0" --overwrite >/dev/null 2>&1 \
    && [ "$(head -c 4 "$d/test.tex0")" = "TEX0" ] \
    && "$B/wimgt" DECODE "$d/test.tex0" --dest "$d/tex0_out.png" --overwrite >/dev/null 2>&1 \
    && [ -s "$d/tex0_out.png" ]; then
      ok "BRRES TEX0 encode -> decode roundtrip"
    else
      no "BRRES TEX0 encode -> decode" "mismatch"
    fi
  fi

  # NCER sprite cell bank
  local f_ncer; f_ncer=$(find_magic "RECN"); [ -n "$f_ncer" ] || f_ncer=$(find_magic "NCER")
  if [ -n "$f_ncer" ]; then
    if "$B/wszst" EXTRACT "$f_ncer" --dest "$d/ncer_out.xml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/ncer_out.xml" --dest "$d/test.ncer" --overwrite >/dev/null 2>&1 \
    && cmp -s "$f_ncer" "$d/test.ncer"; then
      ok "NCER extract -> create roundtrip ($f_ncer)"
    else
      no "NCER extract -> create" "mismatch with $f_ncer"
    fi
  fi

  # NANR sprite animation bank
  local f_nanr; f_nanr=$(find_magic "RNAN"); [ -n "$f_nanr" ] || f_nanr=$(find_magic "NANR")
  if [ -n "$f_nanr" ]; then
    if "$B/wszst" EXTRACT "$f_nanr" --dest "$d/nanr_out.xml" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" CREATE "$d/nanr_out.xml" --dest "$d/test.nanr" --overwrite >/dev/null 2>&1 \
    && cmp -s "$f_nanr" "$d/test.nanr"; then
      ok "NANR extract -> create roundtrip ($f_nanr)"
    else
      no "NANR extract -> create" "mismatch with $f_nanr"
    fi
  fi

  # BRLAN layout animation
  local f_lan; f_lan=$(find_magic "RLAN")
  if [ -n "$f_lan" ]; then
    if "$B/wlayt" decode "$f_lan" "$d/anim.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/anim.tflyt" "$d/anim.brlan" >/dev/null 2>&1 \
    && cmp -s "$f_lan" "$d/anim.brlan"; then
      ok "BRLAN decode -> encode roundtrip ($f_lan)"
    else
      no "BRLAN decode -> encode" "mismatch with $f_lan"
    fi
  fi

  # BRLYT layout
  local f_lyt; f_lyt=$(find_magic "RLYT")
  if [ -n "$f_lyt" ]; then
    if "$B/wlayt" decode "$f_lyt" "$d/layout.tflyt" >/dev/null 2>&1 \
    && "$B/wlayt" encode "$d/layout.tflyt" "$d/layout.brlyt" >/dev/null 2>&1 \
    && cmp -s "$f_lyt" "$d/layout.brlyt"; then
      ok "BRLYT decode -> encode roundtrip ($f_lyt)"
    else
      no "BRLYT decode -> encode" "mismatch with $f_lyt"
    fi
  fi

  # Wii U BFLAN/BFLYT use semantic text round-trips: legal encodings may
  # relocate pointed-to strings/sections while preserving the same tree.
  local f_bflan="$PWD_PROJECT/../tests/fixtures/splatoon_cmn_bg_out.bflan"
  if "$B/wlayt" decode "$f_bflan" "$d/anim-wiiu.tflyt" >/dev/null 2>&1 \
  && "$B/wlayt" encode "$d/anim-wiiu.tflyt" "$d/anim-wiiu.bflan" >/dev/null 2>&1 \
  && "$B/wlayt" decode "$d/anim-wiiu.bflan" "$d/anim-wiiu-2.tflyt" >/dev/null 2>&1 \
  && cmp -s "$d/anim-wiiu.tflyt" "$d/anim-wiiu-2.tflyt"; then
    ok "BFLAN semantic decode -> encode -> decode roundtrip"
  else
    no "BFLAN semantic roundtrip" "$f_bflan"
  fi

  local f_bflyt="$PWD_PROJECT/../tests/fixtures/splatoon_cmn_seq_drc_option.bflyt"
  if "$B/wlayt" decode "$f_bflyt" "$d/layout-wiiu.tflyt" >/dev/null 2>&1 \
  && "$B/wlayt" encode "$d/layout-wiiu.tflyt" "$d/layout-wiiu.bflyt" >/dev/null 2>&1 \
  && "$B/wlayt" decode "$d/layout-wiiu.bflyt" "$d/layout-wiiu-2.tflyt" >/dev/null 2>&1 \
  && cmp -s "$d/layout-wiiu.tflyt" "$d/layout-wiiu-2.tflyt"; then
    ok "BFLYT semantic decode -> encode -> decode roundtrip"
  else
    no "BFLYT semantic roundtrip" "$f_bflyt"
  fi

  # BMG message text. Deliberately NOT a byte-exact round-trip check like the
  # BRLAN/BRLYT ones above: real in-the-wild BMGs (e.g. WiiLink-authored
  # ones) can have pre-existing, unrelated round-trip gaps in attribute
  # encoding ("[,,,2/14]"-style per-message overrides) that have nothing to
  # do with text at all, and would make this test flaky across machines
  # depending on which real .bmg SEARCH happens to find. What this guards
  # against is specific and was a real, confirmed bug: BMG_ENC_UTF16BE text
  # must always be read/written big-endian regardless of the container's
  # own structural endian ('bmg->endian') -- a real Wii System Menu BMG has
  # little-endian structural fields but big-endian text, and the old code
  # used the structural endian for text too. That regression was byte-exact
  # round-trip *clean* (decode and encode both applied the same wrong
  # transformation and it canceled out) -- it only showed up as unreadable
  # (garbled CJK-range) text -- so the check here is specifically "does a
  # real message decode to a real printable word", not "does the archive
  # come back byte-identical".
  local f_bmg found_bmg=0 last_bmg=""
  while IFS= read -r f_bmg; do
    [ -n "$f_bmg" ] || continue
    last_bmg="$f_bmg"
    if "$B/wszst" TEXT "$f_bmg" --dest "$d/msg.bmg.txt" --overwrite >/dev/null 2>&1 \
    && grep -qE '^ *[0-9a-f]+[[:space:]]*=[[:space:]]*[A-Za-z]{3,}' "$d/msg.bmg.txt"; then
      ok "BMG decode produces readable text ($f_bmg)"
      found_bmg=1
      break
    fi
  done < <(awk -F'\t' -v m="MESG" '$1==m || index($1,m)==1{print $2}' "$IDX")
  if [ "$found_bmg" -eq 0 ] && [ -n "$last_bmg" ]; then
    no "BMG decode" "no readable ASCII message found in $last_bmg"
  fi

  # BRFNT font
  if [ -f "$d/img.png" ]; then
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.brfnt" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.brfnt" -d "$d/test_brfnt_out.png" --overwrite >/dev/null 2>&1; then
      ok "BRFNT encode -> decode roundtrip"
    else
      no "BRFNT encode -> decode" "failed"
    fi
    # BRFNA font archive
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.brfna" --overwrite >/dev/null 2>&1 \
    && "$B/wimgt" DECODE "$d/test.brfna" -d "$d/test_brfna_out.png" --overwrite >/dev/null 2>&1; then
      ok "BRFNA encode -> decode roundtrip"
    else
      no "BRFNA encode -> decode" "failed"
    fi
    # BCFNT font
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.bcfnt" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.bcfnt" --dest "$d/bcfnt_out.xml" --overwrite >/dev/null 2>&1; then
      ok "BCFNT encode -> extract XML roundtrip"
    else
      no "BCFNT encode -> extract XML" "failed"
    fi
    # BFFNT font
    if "$B/wimgt" ENCODE "$d/img.png" -d "$d/test.bffnt" --overwrite >/dev/null 2>&1 \
    && "$B/wszst" EXTRACT "$d/test.bffnt" --dest "$d/bffnt_out.xml" --overwrite >/dev/null 2>&1; then
      ok "BFFNT encode -> extract XML roundtrip"
    else
      no "BFFNT encode -> extract XML" "failed"
    fi
  fi

  rm -rf "$d"
}
t_container_roundtrips

echo "== CREATE hash-cache (skip unchanged, rebuild on real change) =="
t_hash_cache(){
  local d; d=$(mktemp -d)
  mkdir -p "$d/tree"
  printf 'plain payload one\n' > "$d/tree/plain.bin"
  printf 'plain payload two\n' > "$d/tree/other.bin"

  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  if [ ! -f "$d/tree/.wszst-cache.txt" ]; then
    no "hash-cache: cache file" "not created after CREATE"
    rm -rf "$d"; return
  fi

  local mtime1; mtime1=$(stat -f %m "$d/out.arc")
  sleep 1
  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  local mtime2; mtime2=$(stat -f %m "$d/out.arc")
  if [ "$mtime1" = "$mtime2" ]; then
    ok "hash-cache: unchanged rebuild skips the rebuild+write entirely"
  else
    no "hash-cache: unchanged rebuild" "out.arc was rewritten though nothing changed"
  fi

  printf 'plain payload one -- edited\n' > "$d/tree/plain.bin"
  "$B/wszst" CREATE "$d/tree" --dest "$d/out.arc" --overwrite >/dev/null 2>&1
  local mtime3; mtime3=$(stat -f %m "$d/out.arc")
  rm -rf "$d/verify"
  "$B/wszst" EXTRACT "$d/out.arc" --dest "$d/verify" --overwrite >/dev/null 2>&1
  if [ "$mtime3" != "$mtime2" ] \
  && cmp -s "$d/tree/plain.bin" "$d/verify/plain.bin" \
  && cmp -s "$d/tree/other.bin" "$d/verify/other.bin"; then
    ok "hash-cache: real change triggers rebuild with correct content"
  else
    no "hash-cache: real change" "dest not rebuilt, or rebuilt content mismatch"
  fi

  rm -rf "$d"
}
t_hash_cache

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

echo "== wbmsx COMTYPE ash0/rl/lzh8/qlz/at7 (native decoders, not stock QuickBMS names) =="
t_wbmsx_native(){
  local d; d=$(mktemp -d)
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > "$d/expected.bin"
  local ok=1
  for e in ash0 rl lzh8 qlz at7; do
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
  [ "$ok" = 1 ] && ok "wbmsx COMTYPE ash0+rl+lzh8+quicklz+at7 round-trip" \
    || no "wbmsx COMTYPE ash0+rl+lzh8+quicklz+at7 round-trip" "mismatch"
}
t_wbmsx_native

echo "== AT7 archive extraction (PMD WiiWare data*.bin / AT7P container) =="
t_at7_container(){
  command -v python3 >/dev/null || { sk "AT7 container extraction"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
f1 = b'Pokemon Mystery Dungeon File 1 Payload\n' * 10
f2 = b'Pokemon Mystery Dungeon File 2 Payload\n' * 15
tlen = 28 * 3
toc = struct.pack('>II', tlen, len(f1)) + b'file1.bin\x00'.ljust(20, b'\x00')
toc += struct.pack('>II', tlen + len(f1), len(f2)) + b'file2.bin\x00'.ljust(20, b'\x00')
toc += b'\x00' * 28
open('$d/raw.bin', 'wb').write(toc + f1 + f2)
"
  local ok=1
  "$B/wszst" COMPRESS "$d/raw.bin" --dest "$d/data0_0001.bin.at7" --overwrite >/dev/null 2>&1
  "$B/wszst" xx "$d/data0_0001.bin.at7" --dest "$d/out.d" --overwrite >/dev/null 2>&1
  [ -f "$d/out.d/file1.bin" ] && [ -f "$d/out.d/file2.bin" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "AT7 container archive extraction (wszst xx)" \
    || no "AT7 container archive extraction" "mismatch"
}
t_at7_container

echo "== Nintendo Huffman (HUFF4 / HUFF8) =="
t_huffman(){
  command -v python3 >/dev/null || { sk "Nintendo Huffman (4-bit & 8-bit)"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
bits = '01011001' + '0'*24
stream = struct.pack('<I', int(bits, 2))
open('$d/huff8.bin', 'wb').write(bytes([0x28, 8, 0, 0, 1, 0xC0, 0x00, ord(\"A\"), ord(\"B\")]) + stream)
open('$d/huff4.bin', 'wb').write(bytes([0x24, 4, 0, 0, 1, 0xC0, 0x00, 1, 2]) + stream)
"
  local ok=1
  "$B/wszst" DECOMPRESS "$d/huff8.bin" --dest "$d/out8.bin" --overwrite >/dev/null 2>&1
  [ "$(cat "$d/out8.bin" 2>/dev/null)" = "ABABBAAB" ] || ok=0
  "$B/wszst" DECOMPRESS "$d/huff4.bin" --dest "$d/out4.bin" --overwrite >/dev/null 2>&1
  [ "$(od -An -tx1 "$d/out4.bin" 2>/dev/null | tr -d ' \n')" = "12122112" ] || ok=0
  printf 'COMTYPE huff8\nCLOG "out8.dat" 0 %d\n' "$(stat -f%z "$d/huff8.bin")" > "$d/test8.bms"
  printf 'COMTYPE huff4\nCLOG "out4.dat" 0 %d\n' "$(stat -f%z "$d/huff4.bin")" > "$d/test4.bms"
  "$B/wbmsx" "$d/test8.bms" "$d/huff8.bin" "$d/out8_bms" >/dev/null 2>&1
  [ "$(cat "$d/out8_bms/out8.dat" 2>/dev/null)" = "ABABBAAB" ] || ok=0
  "$B/wbmsx" "$d/test4.bms" "$d/huff4.bin" "$d/out4_bms" >/dev/null 2>&1
  [ "$(od -An -tx1 "$d/out4_bms/out4.dat" 2>/dev/null | tr -d ' \n')" = "12122112" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "Nintendo Huffman 4-bit + 8-bit (wszst + wbmsx)" \
    || no "Nintendo Huffman 4-bit + 8-bit" "mismatch"
}
t_huffman

echo "== Mario Party BIN (wmpbpack/wmpbdump, Hudson mpbin-tools port) =="
# The synthetic round-trip still catches encoder/decoder symmetry. A curated
# mariomdl0.bin from retail GMPE01 (Mario Party 4 USA Rev 1) separately proves
# the real Hudson fast-slide path and yields two duplicate HSFV037 models.
# Both tools used to call
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
    ( cd "$d" && timeout 10 "$PWD_PROJECT/bin/wszst" xx "out$ct.bin" --dest "out${ct}_xx.d" --overwrite >/dev/null 2>&1
      cmp -s "out${ct}_xx.d/file000.dat" in.dat ) || all_ok=0
  done
  rm -rf "$d"
  [ "$all_ok" = 1 ] && ok "Mario Party BIN round-trip (compress_type 0/1/2/5/7, synthetic + wszst xx)" \
    || no "Mario Party BIN round-trip" "one or more compress_type mismatched"
}
t_mpb

echo "== QuickBMS chaining (wszst xx --bms) =="
t_wszst_bms(){
  command -v python3 >/dev/null || { sk "wszst xx --bms"; return; }
  local d; d=$(mktemp -d)
  printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..200} > "$d/payload.dat"
  python3 -c "import zlib; open('$d/container.bin', 'wb').write(zlib.compress(open('$d/payload.dat', 'rb').read()))"
  printf 'COMTYPE zlib\nCLOG "nested.dat" 0 %d\n' "$(stat -f%z "$d/container.bin")" > "$d/unpack.bms"
  "$B/wszst" xx "$d/container.bin" --bms="$d/unpack.bms" --dest "$d/out_bms" --overwrite >/dev/null 2>&1
  if [ -s "$d/out_bms/container.d/nested.dat" ] && cmp -s "$d/out_bms/container.d/nested.dat" "$d/payload.dat"; then
    ok "wszst xx --bms chained extraction"
  else
    no "wszst xx --bms chained extraction" "nested.dat mismatch or missing"
  fi
  rm -rf "$d"
}
t_wszst_bms

echo "== AJPG (wimgt native decode/encode) =="
t_ajpg_wimgt(){
  command -v python3 >/dev/null || { sk "wimgt AJPG"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct, zlib
def make_png(w, h):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    raw = b''.join(b'\x00' + b'\x80\x40\x20\xff' * w for _ in range(h))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open('$d/test.png', 'wb').write(make_png(64, 64))
"
  "$B/wimgt" ENCODE "$d/test.png" --dest "$d/test.ajpg" --overwrite >/dev/null 2>&1
  "$B/wimgt" DECODE "$d/test.ajpg" --dest "$d/out.png" --overwrite >/dev/null 2>&1
  if [ -s "$d/test.ajpg" ] && [ -s "$d/out.png" ]; then
    ok "AJPG encoded and decoded via wimgt"
  else
    no "AJPG via wimgt" "failed to encode or decode AJPG"
  fi
  rm -rf "$d"
}
t_ajpg_wimgt

t_mpb_retail(){
  local src="$PWD_PROJECT/../tests/fixtures/mp4_mariomdl0.bin"
  [ -f "$src" ] || { sk "Mario Party 4 retail BIN"; return; }
  local d
  d=$(mktemp -d /tmp/_r_mp4_retail.XXXXXX) || { no "Mario Party 4 retail BIN" "mktemp failed"; return; }
  cp "$src" "$d/model.bin"
  ( cd "$d" && timeout 15 "$PWD_PROJECT/wmpbdump" model.bin >run.log 2>&1 )
  "$B/wszst" EXTRACT "$d/model.bin" --dest "$d/decoded" --overwrite >"$d/wszst.log" 2>&1
  local a="$d/model_file0.hsf" b="$d/model_file1.hsf"
  local glb="$d/decoded/file000.glb"
  local motion="$glb.motion.json"
  local textured=0
  if [ -s "$glb" ]; then
    textured=$(python3 - "$glb" <<'PY'
import json,struct,sys
d=open(sys.argv[1],'rb').read(); n=struct.unpack_from('<I',d,12)[0]
j=json.loads(d[20:20+n]); p=[p for m in j.get('meshes',[]) for p in m['primitives']]
print(int(len(j.get('images',[])) >= 5 and len(j.get('nodes',[])) >= 100
    and len(j.get('skins',[])) == 1 and sum('JOINTS_0' in x['attributes'] for x in p) == 15
    and sum('WEIGHTS_0' in x['attributes'] for x in p) == 15))
PY
)
  fi
  if [ -s "$a" ] && [ -s "$b" ] \
      && [ "$(head -c 7 "$a")" = HSFV037 ] \
      && cmp -s "$a" "$b" \
      && ! grep -q 'Failed\|Unknown Compression' "$d/run.log" \
      && [ "$textured" = 1 ] && [ "$(find "$d/decoded" -name '*.png' | wc -l)" -ge 5 ] \
      && python3 -c 'import json,sys;j=json.load(open(sys.argv[1]));assert len(j["motions"][0]["tracks"])==1082' "$motion"; then
    ok "Mario Party 4 retail BIN -> 2 textured, hierarchical, skinned HSF models"
  else
    no "Mario Party 4 retail BIN" "$src"
  fi
}
t_mpb_retail

t_hsf_runtime_features(){
  local src="$PWD_PROJECT/../tests/fixtures/hsf-features" d
  [ -d "$src" ] || { sk "HSF runtime feature fixtures"; return; }
  d=$(mktemp -d /tmp/_r_hsf_features.XXXXXX) || return
  "$B/wszst" EXTRACT "$src/cluster.hsf" --dest "$d/cluster.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica-shape.hsf" --dest "$d/shape.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica.hsf" --dest "$d/replica.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/camera.hsf" --dest "$d/camera.glb" --overwrite >/dev/null 2>&1 || true
  "$B/wszst" EXTRACT "$src/light.hsf" --dest "$d/light.glb" --overwrite >/dev/null 2>&1 || true
  "$B/wszst" EXTRACT "$src/nested-replica.hsf" --dest "$d/nested.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$src/replica.hsf" --dest "$d/encode.dae" --overwrite >/dev/null 2>&1
  "$B/wmdlt" ENCODE "$d/encode.dae" --dest "$d/encoded.hsf" >/dev/null 2>&1
  "$B/wszst" EXTRACT "$d/encoded.hsf" --dest "$d/encoded.glb" --overwrite >/dev/null 2>&1
  "$B/wszst" EXTRACT "$PWD_PROJECT/../tests/fixtures/hsf_multipart_test.hsf" --dest "$d/textured.dae" --overwrite >/dev/null 2>&1
  "$B/wmdlt" ENCODE "$d/textured.dae" --dest "$d/textured.hsf" >/dev/null 2>&1
  "$B/wszst" EXTRACT "$d/textured.hsf" --dest "$d/textured.glb" --overwrite >/dev/null 2>&1
  if python3 - "$d/cluster.glb" "$d/shape.glb" "$d/replica.glb" "$d/camera.glb.hsf.json" "$d/light.glb.hsf.json" "$d/nested.glb" "$d/encoded.hsf" "$d/encoded.glb" "$d/textured.hsf" "$d/textured.glb" <<'PY'
import json,struct,sys
def glb(path):
 d=open(path,'rb').read(); n=struct.unpack_from('<I',d,12)[0]; return json.loads(d[20:20+n])
c,s,r=map(glb,sys.argv[1:])
assert len(c['meshes'][0]['primitives'][0]['targets']) == 7
assert len(s['meshes'][0]['primitives'][0]['targets']) == 34
assert s['meshes'][0]['extras']['targetNames'][0] == 'flaga_shape1'
for j,n in ((c,7),(s,34)):
 a=j['animations'][0]; wi=next(i for i,x in enumerate(a['channels']) if x['target']['path']=='weights')
 ia=j['accessors'][a['samplers'][wi]['input']]; oa=j['accessors'][a['samplers'][wi]['output']]
 assert oa['type']=='SCALAR' and oa['count']==ia['count']*n
assert any(x['target']['path']=='rotation' for x in s['animations'][0]['channels'])
mesh_nodes=[x for x in r['nodes'] if 'mesh' in x]
assert len(mesh_nodes) > len(r['meshes']) and all('matrix' in x for x in mesh_nodes[len(r['meshes']):])
camera=json.load(open(sys.argv[4])); light=json.load(open(sys.argv[5]))
assert len(camera['cameras'])==1 and camera['cameras'][0]['far'] > camera['cameras'][0]['near']
assert len(light['lights'])==1 and len(light['lights'][0]['color'])==3
assert camera['scene'] and light['scene']
cg,lg=glb(sys.argv[4][:-9]),glb(sys.argv[5][:-9])
assert len(cg['cameras'])==1 and any('camera' in n and len(n['matrix'])==16 for n in cg['nodes'])
assert lg['extensionsUsed']==['KHR_lights_punctual']
assert len(lg['extensions']['KHR_lights_punctual']['lights'])==1
nested=glb(sys.argv[6]); nested_nodes=[x for x in nested['nodes'] if 'mesh' in x]
assert len(nested_nodes)-len(nested['meshes'])==51
encoded=open(sys.argv[7],'rb').read(); roundtrip=glb(sys.argv[8])
assert encoded.startswith(b'HSFV037') and len(roundtrip['meshes'])==len(r['meshes'])
textured=open(sys.argv[9],'rb').read(); textured_glb=glb(sys.argv[10])
assert struct.unpack_from('>I',textured,12+3*8)[0]==1
assert struct.unpack_from('>I',textured,12+9*8)[0]==1 and len(textured_glb['images'])==1
PY
  then ok "HSF retail morphs, replicas, cameras, lights and scene metadata"
  else no "HSF runtime features" "$src"; fi
}
t_hsf_runtime_features

t_mpb_modify_repack_roundtrip(){
  # Full "modify one file, repack the container" workflow on a real retail
  # archive, not synthetic data: extract -> flip a byte inside one leaf file
  # -> wszst create -> re-extract -> the touched file must carry the edit and
  # every untouched sibling file must still be byte-identical, proving the
  # repack neither drops nor corrupts unrelated content.
  local src="$PWD_PROJECT/../tests/fixtures/mp4_mariomdl0.bin"
  [ -f "$src" ] || { sk "MPBIN modify+repack round-trip"; return; }
  local d; d=$(mktemp -d /tmp/_r_mpb_rt.XXXXXX) || { no "MPBIN modify+repack round-trip" "mktemp failed"; return; }
  $B/wszst xx "$src" --dest "$d/orig" --overwrite >"$d/xx1.log" 2>&1
  # 'xx' also decodes each .hsf leaf to a sidecar .glb (now that multi-part
  # HSF models decode too, both leaves in this fixture do) -- those are
  # derived convenience output, not raw container members, so they don't
  # belong in the repack input.
  rm -f "$d"/orig/*.glb
  local target; target=$(find "$d/orig" -maxdepth 1 -type f -name '*.hsf' | sort | tail -1)
  if [ -z "$target" ]; then no "MPBIN modify+repack round-trip" "no extracted files"; rm -rf "$d"; return; fi
  python3 -c "
import sys
p = '$target'
d = bytearray(open(p, 'rb').read())
d[0x100] ^= 0xFF
open(p, 'wb').write(d)
"
  $B/wszst create "$d/orig" --dest "$d/repack.bin" --overwrite >"$d/create.log" 2>&1
  $B/wszst xx "$d/repack.bin" --dest "$d/reext" --overwrite >"$d/xx2.log" 2>&1
  local tname; tname=$(basename "$target")
  local all_ok=1
  cmp -s "$target" "$d/reext/$tname" || all_ok=0
  for f in "$d/orig"/*; do
    local bn; bn=$(basename "$f")
    [ "$bn" = "$tname" ] && continue
    cmp -s "$f" "$d/reext/$bn" || all_ok=0
  done
  if [ "$all_ok" = 1 ]; then
    ok "MPBIN modify+repack round-trip (edit survives, siblings untouched)"
  else
    no "MPBIN modify+repack round-trip" "edited or sibling file mismatched after repack"
  fi
  rm -rf "$d"
}
t_mpb_modify_repack_roundtrip

echo "== Monster Games RST / TOC (0TSR / 0SERCOTE) =="
t_rst_container(){
  command -v python3 >/dev/null || { sk "RST container roundtrip"; return; }
  local d; d=$(mktemp -d)
  mkdir -p "$d/rst_src"
  printf 'Monster Games Texture Model 0 Payload\n%.0s' {1..20} > "$d/rst_src/car.tm0"
  printf 'Monster Games Model Payload\n%.0s' {1..30} > "$d/rst_src/car.mod"
  printf 'Monster Games Values Payload\n%.0s' {1..10} > "$d/rst_src/car.val"
  local ok=1
  "$B/wszst" CREATE "$d/rst_src" --dest "$d/test.car" --overwrite >/dev/null 2>&1
  [ -f "$d/test.car" ] && [ -f "$d/test.toc" ] || ok=0
  # First v3 entry offset (+0x10 in TOC record 1) must be payload-relative 0.
  python3 - "$d/test.toc" <<'PYEOF' || ok=0
import struct, sys
toc = open(sys.argv[1], "rb").read()
assert toc[:8] == b"0SERCOTE"
assert struct.unpack_from("<I", toc, 0x0c + 0x28 + 0x10)[0] == 0
PYEOF
  "$B/wszst" EXTRACT "$d/test.car" --dest "$d/rst_out" --overwrite >/dev/null 2>&1
  local src_dir="$d/rst_out/test"
  [ -d "$src_dir" ] || src_dir="$d/rst_out"
  cmp -s "$d/rst_src/car.tm0" "$src_dir/car.tm0" || ok=0
  cmp -s "$d/rst_src/car.mod" "$src_dir/car.mod" || ok=0
  cmp -s "$d/rst_src/car.val" "$src_dir/car.val" || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "RST + TOC container create & extract roundtrip" \
    || no "RST container" "mismatch or failed extraction"
}
t_rst_container

echo "== WarioWare Snapped! Nitro size-prefix wrapper =="
t_wwsnapped_wrapper(){
  # WarioWare: D.I.Y. Showcase / "WarioWare Snapped!" (DSiWare, NTR-KUWE) wraps
  # every LZ11-compressed Nitro graphics resource (NCGR/NCLR/NCER/NANR) in a
  # 4-byte little-endian size-prefix record before the real magic. Verified
  # against 5 real retail assets (Style/StyleO.NCLR.bin, Style/Style_Head.
  # NCGR.bin, Style/Style_Head.NCER.bin, Style/Style_2P_01.NANR.bin, Game/
  # WarningB.NCLR.bin) extracted from the USA Rev1 ROM. This builds a small
  # synthetic NCLR wrapped the same way and LZ11-compresses it, then checks
  # that `wimgt DECODE` -- which used to fail with "?" / unrecognized format
  # because the magic no longer sat at offset 0 -- now decodes it end to end.
  command -v python3 >/dev/null || { sk "WarioWare Snapped wrapper"; return; }
  local d; d=$(mktemp -d)
  python3 - "$d" <<'PYEOF'
import struct, sys
d = sys.argv[1]
pltt_body = struct.pack('<2H', 0x0000, 0x7fff)
pltt_hdr = struct.pack('<IIII', 4, len(pltt_body), 0x10, 0)
ttlp_sec = b'TTLP' + struct.pack('<I', 8 + len(pltt_hdr) + len(pltt_body)) + pltt_hdr + pltt_body
nclr = b'RLCN' + struct.pack('<HH', 0xfffe, 1) + struct.pack('<I', 0) + struct.pack('<HH', 0x10, 1) + ttlp_sec
nclr = nclr[:8] + struct.pack('<I', len(nclr)) + nclr[12:]
wrapped = struct.pack('<BHB', 0, len(nclr), 0) + nclr
open(d + '/synth_wrapped.bin', 'wb').write(wrapped)
PYEOF
  local ok=1
  "$B/wszst" COMPRESS "$d/synth_wrapped.bin" --dest "$d/synth_wrapped.lz11" --overwrite >/dev/null 2>&1
  [ -s "$d/synth_wrapped.lz11" ] || ok=0
  "$B/wimgt" DECODE "$d/synth_wrapped.lz11" --dest "$d/synth_wrapped.png" --overwrite >/dev/null 2>&1
  [ -s "$d/synth_wrapped.png" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "WarioWare Snapped Nitro size-prefix wrapper -> decode" \
    || no "WarioWare Snapped wrapper" "LZ11-wrapped NCLR failed to decode"
}
t_wwsnapped_wrapper

echo "== THP Video Extraction =="
t_thp_extract(){
  command -v python3 >/dev/null || { sk "THP video extraction"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct
jpeg_data = bytes.fromhex(
    'ffd8ffe000104a46494600010101004800480000ffdb004300080606070605080707070909080a0c140d0c0b0b0c1912130f141d1a1f1e1d1a1c1c20242e2720222c231c1c2837292c30313434341f27393d38323c2e333432ffc0000b080010001001011100ffda0008010100003f00bf00ffd9'
)
movie_off = 0x60
comp_data_off = 0x30
offset_data_off = 0x50

frame_comp_sz = len(jpeg_data)
frame_sz = 12 + frame_comp_sz
hdr = struct.pack('>4sIIIfIIIIIII', b'THP\0', 0x11000, 0x10000, 0, 30.0, 1, frame_sz, frame_sz, comp_data_off, offset_data_off, movie_off, movie_off)
comps = struct.pack('>I16BIII', 1, 0, *([0]*15), 16, 16, 0)
comps_padded = comps.ljust(offset_data_off - comp_data_off, b'\x00')
offsets = struct.pack('>II', movie_off, 0)
offsets_padded = offsets.ljust(movie_off - offset_data_off, b'\x00')
frame_hdr = struct.pack('>III', frame_sz, 0, frame_comp_sz)
thp_bytes = hdr + comps_padded + offsets_padded + frame_hdr + jpeg_data
open('$d/test.thp', 'wb').write(thp_bytes)
"
  local ok=1
  "$B/wszst" EXTRACT "$d/test.thp" --dest "$d/thp_out" --no-passthrough --overwrite >/dev/null 2>&1
  local out_dir="$d/thp_out/test"
  [ -d "$out_dir" ] || out_dir="$d/thp_out"
  [ -s "$out_dir/frame_00000.jpg" ] || ok=0
  rm -rf "$d"
  [ "$ok" = 1 ] && ok "THP frame extraction (wszst EXTRACT)" \
    || no "THP extraction" "frame_00000.jpg missing or empty"
}
t_thp_extract

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
  local sf2_count; sf2_count=$(find /tmp/_r_brsar -iname "*.sf2" -size +100c 2>/dev/null | wc -l | tr -d ' ')
  if [ -n "$mid" ] && [ "$sf2_count" -eq 1 ]; then
    ok "BRSAR -> MIDI + single SF2 ($f)"
  elif [ -n "$mid" ]; then
    ok "BRSAR -> MIDI ($f)"
  else
    no "BRSAR -> MIDI" "no candidate produced a valid MIDI"
  fi
}
t_brsar

t_sdat(){
  local candidates; candidates=$(awk -F'\t' '$1=="SDAT"{print $2}' "$IDX" 2>/dev/null)
  if [ -z "$candidates" ]; then
    local f_cand=""
    for d in $SEARCH; do [ -d "$d" ] || continue
      f_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.sdat' 2>/dev/null | head -1)
      [ -n "$f_cand" ] && break
    done
    candidates="$f_cand"
  fi
  [ -n "$candidates" ] || { sk "SDAT (DS sound archive)"; return; }
  local f mid
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    rm -rf /tmp/_r_sdat; mkdir -p /tmp/_r_sdat
    $B/wbrsar "$f" /tmp/_r_sdat >/tmp/_r_sdat.log 2>&1
    mid=$(find /tmp/_r_sdat -iname "*.mid" -size +14c 2>/dev/null | head -1)
    [ -n "$mid" ] && [ "$(head -c4 "$mid")" = "MThd" ] && break
    mid=""
  done <<< "$candidates"
  local sf2_count; sf2_count=$(find /tmp/_r_sdat -iname "*.sf2" -size +100c 2>/dev/null | wc -l | tr -d ' ')
  if [ -n "$mid" ] && [ "$sf2_count" -ge 1 ]; then
    ok "SDAT -> MIDI + SF2 ($f)"
  elif [ -n "$mid" ]; then
    ok "SDAT -> MIDI ($f)"
  else
    no "SDAT -> MIDI" "no candidate produced a valid MIDI"
  fi
}
t_sdat

t_bcsar_bfsar(){
  # Prefer the committed fixtures: they are known-good "full" archives
  # (BCSEQ/BCWAR/BCWSD/BCBNK + nested BCWAV, or the BFSAR equivalent), so the
  # result is deterministic regardless of what happens to be on this machine.
  # A prior version of this test scanned $SEARCH / /Volumes/SSD/dlz for any
  # *.bcsar/*.bfsar and took whatever `find` returned first -- traversal
  # order isn't stable, and some real-world archives are SFX-only (raw
  # BCWAV, no sequence/bank data), so the test would flakily pass or fail
  # depending on which file got picked that run. Fall back to that scan only
  # if the fixture is somehow missing.
  local bcsar_cand="$PWD_PROJECT/../tests/fixtures/sample.bcsar"
  local bfsar_cand="$PWD_PROJECT/../tests/fixtures/sample.bfsar"
  [ -s "$bcsar_cand" ] || bcsar_cand=""
  [ -s "$bfsar_cand" ] || bfsar_cand=""

  if [ -z "$bcsar_cand" ] || [ -z "$bfsar_cand" ]; then
    for d in $SEARCH "/Volumes/SSD/dlz"; do [ -d "$d" ] || continue
      [ -z "$bcsar_cand" ] && bcsar_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.bcsar' 2>/dev/null | head -1)
      [ -z "$bfsar_cand" ] && bfsar_cand=$(find -L "$d" -maxdepth 8 -type f -size +100c -iname '*.bfsar' 2>/dev/null | head -1)
      [ -n "$bcsar_cand" ] && [ -n "$bfsar_cand" ] && break
    done
  fi

  if [ -n "$bcsar_cand" ] && [ -s "$bcsar_cand" ]; then
    local out_bcsar="/tmp/_r_bcsar"
    local repack_bcsar="/tmp/_r_repack.bcsar"
    rm -rf "$out_bcsar" "$repack_bcsar"
    mkdir -p "$out_bcsar"
    $B/wszst xx "$bcsar_cand" --dest "$out_bcsar" >/tmp/_r_bcsar.log 2>&1
    local cseq_count; cseq_count=$(find "$out_bcsar" -type f \( -iname "*.bcseq" -o -iname "*.bcwar" -o -iname "*.bcwsd" -o -iname "*.bcbnk" \) 2>/dev/null | wc -l | tr -d ' ')
    local sub_bcwav_count; sub_bcwav_count=$(find "$out_bcsar" -type f -iname "*.bcwav" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$cseq_count" -ge 1 ] && [ "$sub_bcwav_count" -ge 1 ]; then
      ok "BCSAR extract + recursive BCWAR ($bcsar_cand)"
      local first_sub; first_sub=$(find "$out_bcsar" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)
      if [ -n "$first_sub" ]; then
        $B/wszst create "$first_sub" --dest "$repack_bcsar" >/dev/null 2>&1
        if [ -s "$repack_bcsar" ] && [ "$(head -c4 "$repack_bcsar")" = "CSAR" ]; then
          ok "BCSAR repack (wszst create -> CSAR)"
        else
          no "BCSAR repack" "$repack_bcsar"
        fi
      fi
    else
      no "BCSAR extract" "no expected files found in $out_bcsar"
    fi
  else
    sk "BCSAR (3DS sound archive)"
  fi

  if [ -n "$bfsar_cand" ] && [ -s "$bfsar_cand" ]; then
    local out_bfsar="/tmp/_r_bfsar"
    local repack_bfsar="/tmp/_r_repack.bfsar"
    rm -rf "$out_bfsar" "$repack_bfsar"
    mkdir -p "$out_bfsar"
    $B/wszst xx "$bfsar_cand" --dest "$out_bfsar" >/tmp/_r_bfsar.log 2>&1
    local fseq_count; fseq_count=$(find "$out_bfsar" -type f \( -iname "*.bfseq" -o -iname "*.bfwar" -o -iname "*.bfwsd" -o -iname "*.bfbnk" \) 2>/dev/null | wc -l | tr -d ' ')
    local sub_bfwav_count; sub_bfwav_count=$(find "$out_bfsar" -type f -iname "*.bfwav" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$fseq_count" -ge 1 ] && [ "$sub_bfwav_count" -ge 1 ]; then
      ok "BFSAR extract + recursive BFWAR ($bfsar_cand)"
      local first_sub; first_sub=$(find "$out_bfsar" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)
      if [ -n "$first_sub" ]; then
        $B/wszst create "$first_sub" --dest "$repack_bfsar" >/dev/null 2>&1
        if [ -s "$repack_bfsar" ] && [ "$(head -c4 "$repack_bfsar")" = "FSAR" ]; then
          ok "BFSAR repack (wszst create -> FSAR)"
        else
          no "BFSAR repack" "$repack_bfsar"
        fi
      fi
    else
      no "BFSAR extract" "no expected files found in $out_bfsar"
    fi
  else
    sk "BFSAR (Wii U / Switch sound archive)"
  fi
}
t_bcsar_bfsar



echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo "== Message Studio (MSBT / MSBP / MSBF) =="
t_msbt_roundtrip() {
  local D=/tmp/_r_msbt
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbt"
# MSBT: Message Studio Binary Text (BigEndian, UTF-16)
# Entries: 3

[Greeting]
Hello, world!\nWelcome to Message Studio!

[PlayerName]
Welcome <control group="1" type="2" data="00FF"/>, hero of time!

[ExitMsg]
Goodbye!
EOF

  # 1. wbmgt ENCODE
  $B/wbmgt ENCODE "$D/source.tmsbt" --dest "$D/binary.msbt" >/dev/null 2>&1
  if [ ! -s "$D/binary.msbt" ] || [ "$(head -c8 "$D/binary.msbt")" != "MsgStdBn" ]; then
    no "MSBT encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBT encode (wbmgt -> MsgStdBn)"

  # 2. wbmgt LIST
  if $B/wbmgt LIST "$D/binary.msbt" 2>&1 | grep -q "Greeting"; then
    ok "MSBT list (wbmgt LIST)"
  else
    no "MSBT list (wbmgt LIST)" "label Greeting not found in output"
  fi

  # 3. wbmgt DECODE roundtrip
  $B/wbmgt DECODE "$D/binary.msbt" --dest "$D/decoded.tmsbt" >/dev/null 2>&1
  if grep -q "Greeting" "$D/decoded.tmsbt" && grep -q "hero of time" "$D/decoded.tmsbt"; then
    ok "MSBT decode roundtrip (wbmgt DECODE)"
  else
    no "MSBT decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  # 4. wszst EXTRACT
  $B/wszst EXTRACT "$D/binary.msbt" --dest "$D/wszst_out.txt" >/dev/null 2>&1
  if [ -s "$D/wszst_out.txt" ] && grep -q "Greeting" "$D/wszst_out.txt"; then
    ok "MSBT decode (wszst EXTRACT)"
  else
    no "MSBT decode (wszst EXTRACT)" "wszst failed to extract/decode MSBT"
  fi

  # 5. wszst xx inside archive
  mkdir -p "$D/arch"
  cp "$D/binary.msbt" "$D/arch/message.msbt"
  $B/wszst CREATE "$D/arch" --dest "$D/arch.szs" >/dev/null 2>&1
  $B/wszst xx "$D/arch.szs" --dest "$D/arch_out" >/dev/null 2>&1
  if [ -s "$D/arch_out/message.msbt.txt" ] && grep -q "Greeting" "$D/arch_out/message.msbt.txt"; then
    ok "MSBT auto-decode on wszst xx archive extraction"
  else
    no "MSBT auto-decode on wszst xx archive extraction" "message.msbt.txt not produced"
  fi

  rm -rf "$D"
}
t_msbt_roundtrip

t_msbp_roundtrip() {
  local D=/tmp/_r_msbp
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbp"
# MSBP: Message Studio Binary Project (BigEndian, UTF-16)

[Colors: 2]
  #0: Red = #FF0000FF
  #1: Green = #00FF00FF
EOF

  $B/wbmgt ENCODE "$D/source.tmsbp" --dest "$D/project.msbp" >/dev/null 2>&1
  if [ ! -s "$D/project.msbp" ] || [ "$(head -c8 "$D/project.msbp")" != "MsgPrjBn" ]; then
    no "MSBP encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBP encode (wbmgt -> MsgPrjBn)"

  $B/wbmgt DECODE "$D/project.msbp" --dest "$D/decoded.tmsbp" >/dev/null 2>&1
  if grep -q "Red" "$D/decoded.tmsbp" && grep -q "FF0000FF" "$D/decoded.tmsbp"; then
    ok "MSBP decode roundtrip (wbmgt DECODE)"
  else
    no "MSBP decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  rm -rf "$D"
}
t_msbp_roundtrip

t_msbf_roundtrip() {
  local D=/tmp/_r_msbf
  rm -rf "$D"; mkdir -p "$D"
  cat << 'EOF' > "$D/source.tmsbf"
# MSBF: Message Studio Binary Flowchart (BigEndian)
# Nodes: 3

[Node #0 (Start)]
  type = EntryPoint (next=1)

[Node #1 (Talk)]
  type = Message (msg_index=0, next=2)

[Node #2 (Finish)]
  type = Event (event_id=10, param=0x20, next=65535)
EOF

  $B/wbmgt ENCODE "$D/source.tmsbf" --dest "$D/flow.msbf" >/dev/null 2>&1
  if [ ! -s "$D/flow.msbf" ] || [ "$(head -c8 "$D/flow.msbf")" != "MsgFlwBn" ]; then
    no "MSBF encode (wbmgt)" "output missing or magic mismatch"
    return
  fi
  ok "MSBF encode (wbmgt -> MsgFlwBn)"

  $B/wbmgt DECODE "$D/flow.msbf" --dest "$D/decoded.tmsbf" >/dev/null 2>&1
  if grep -q "Start" "$D/decoded.tmsbf" && grep -q "Finish" "$D/decoded.tmsbf"; then
    ok "MSBF decode roundtrip (wbmgt DECODE)"
  else
    no "MSBF decode roundtrip (wbmgt DECODE)" "decoded file content mismatch"
  fi

  rm -rf "$D"
}
t_msbf_roundtrip

echo "== early DS BMD (SM64DS proprietary binary) -> DAE & texture export =="
t_early_bmd_dae(){
  command -v python3 >/dev/null || { sk "early DS BMD export"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct

shapes_off = 0x3c
dl_off = 0x58
bone_off = 0x80
bone_name_off = 0xc0
mat_off = 0xd0
mat_name_off = 0xe4
plt_off = 0xf0
plt_name_off = 0x100
data_off = 0x120

h = [0, 1, bone_off, 1, shapes_off, 1, mat_off, 1, plt_off, 0, 0, 0, 0, 0, data_off]
hdr = struct.pack('<15I', *h)
shapes = struct.pack('<7I', 1, 0x44, 1, 0x54, 0x28, dl_off, 0)
w1_cmds = bytes([0x40, 0x22, 0x23, 0x23])
w1_params = struct.pack('<I hh 2h 2h 2h 2h', 0, 0,0, 0,0, 0,0, 0x1000,0, 0,0)
w2_cmds = bytes([0x23, 0, 0, 0])
w2_params = struct.pack('<2h 2h', 0,0x1000, 0,0)
dl = w1_cmds + w1_params + w2_cmds + w2_params

bone = struct.pack('<IIIIiiiiiiiiIIII', 0, bone_name_off, 0, 0, 0x1000, 0x1000, 0x1000, 0, 0, 0, 0, 0, 0, 0, 0, 0)
bone_name = b'root\x00\x00\x00\x00'

mat = struct.pack('<IIIII', mat_name_off, data_off, 0, 16 | (16 << 16), 0)
mat_name = b'mat_sample\x00\x00'

plt = struct.pack('<IIII', plt_name_off, data_off + 256, 0, 0)
plt_name = b'mat_sample_pl\x00'

pixels = bytes([i % 16 for i in range(256)])
palette = struct.pack('<16H', *[i * 0x421 for i in range(16)])

data = bytearray(0x280)
data[0:len(hdr)] = hdr
data[shapes_off:shapes_off+len(shapes)] = shapes
data[dl_off:dl_off+len(dl)] = dl
data[bone_off:bone_off+len(bone)] = bone
data[bone_name_off:bone_name_off+len(bone_name)] = bone_name
data[mat_off:mat_off+len(mat)] = mat
data[mat_name_off:mat_name_off+len(mat_name)] = mat_name
data[plt_off:plt_off+len(plt)] = plt
data[plt_name_off:plt_name_off+len(plt_name)] = plt_name
data[data_off:data_off+len(pixels)+len(palette)] = pixels + palette

with open('$d/test.bmd', 'wb') as f:
    f.write(data)
"
  "$B/wmdlt" ENCODE "$d/test.bmd" -d "$d/test.dae" --overwrite >/dev/null 2>&1
  local ok=1
  [ -s "$d/test.dae" ] || ok=0
  [ -s "$d/mat_sample.png" ] || ok=0
  grep -q "mat_sample.png" "$d/test.dae" || ok=0
  grep -q "mesh_0" "$d/test.dae" || ok=0
  python3 "$DAE_VALIDATOR" "$d/test.dae" >/dev/null 2>&1 || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "early DS BMD -> DAE + PNG textures" \
    || no "early DS BMD -> DAE + PNG textures" "conversion failed"
}
t_early_bmd_dae

echo "== NintendoWare sequence (RSEQ, CSEQ, FSEQ, SSEQ) & MIDI roundtrips =="
t_sequence_roundtrips(){
  local d; d=$(mktemp -d)
  cat << 'EOF' > "$d/song.txt"
; Test Nintendo Sequence
timebase 48
alloc_track 0x0003
open_track 1 @Track1
tempo 120
vol 127
pan 64
prg 0
note C4 100 48
wait 48
note E4 100 48
wait 48
note G4 100 96
wait 96
jump @Loop

@Loop:
note C5 100 96
wait 96
jump @Loop

@Track1:
prg 1
vol 110
pan 80
note C3 90 96
wait 96
note G3 90 96
wait 96
fin
EOF

  local ok=1
  # Test RSEQ, CSEQ, FSEQ (Wii U & Switch), SSEQ assembly
  "$B/wseqt" asm "$d/song.txt" "$d/song.rseq" --format RSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song.cseq" --format CSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song_wiiu.fseq" --format FSEQ >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song_nx.fseq" --format FSEQ_LE >/dev/null 2>&1 || ok=0
  "$B/wseqt" asm "$d/song.txt" "$d/song.sseq" --format SSEQ >/dev/null 2>&1 || ok=0

  # Test disassembly
  "$B/wseqt" disasm "$d/song.rseq" "$d/song_dis.txt" >/dev/null 2>&1 || ok=0
  grep -q "timebase 48" "$d/song_dis.txt" || ok=0
  grep -q "tempo 120" "$d/song_dis.txt" || ok=0
  grep -q "note C4" "$d/song_dis.txt" || ok=0

  # Test MIDI conversion roundtrip
  "$B/wseqt" to_midi "$d/song.rseq" "$d/song.mid" >/dev/null 2>&1 || ok=0
  [ -s "$d/song.mid" ] || ok=0
  "$B/wseqt" from_midi "$d/song.mid" "$d/song_midi.rseq" --format RSEQ >/dev/null 2>&1 || ok=0
  [ -s "$d/song_midi.rseq" ] || ok=0

  # Test sequence invert
  "$B/wseqt" invert "$d/song.rseq" "$d/song_inv.rseq" --center 63 >/dev/null 2>&1 || ok=0
  [ -s "$d/song_inv.rseq" ] || ok=0

  # Test wszst CREATE and EXTRACT
  "$B/wszst" CREATE "$d/song.txt" --dest "$d/wszst_song.rseq" --overwrite >/dev/null 2>&1 || ok=0
  [ -s "$d/wszst_song.rseq" ] || ok=0
  mkdir -p "$d/out"
  "$B/wszst" EXTRACT "$d/wszst_song.rseq" --dest "$d/out/" --overwrite >/dev/null 2>&1 || ok=0
  [ -s "$d/out/wszst_song.txt" ] || ok=0
  [ -s "$d/out/wszst_song.mid" ] || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "NintendoWare sequence RSEQ/CSEQ/FSEQ/SSEQ/MIDI roundtrips & wszst integration" \
    || no "NintendoWare sequence RSEQ/CSEQ/FSEQ/SSEQ/MIDI roundtrips & wszst integration" "sequence test failed"
}
t_sequence_roundtrips

t_brstm_roundtrip(){
  # BRSTM ADPCM_THP encode->decode roundtrip. from_wav prefers passing
  # through to mobipeg's real adpcm_thp encoder (PassthruEncodeAudio(),
  # lib-passthru.c) and falls back to this project's own EncodeBRSTM() port
  # when mobipeg isn't installed or predates the brstm/dsp/bns muxers --
  # this environment has no mobipeg on PATH, so this exercises the fallback;
  # PATH a real mobipeg in to additionally exercise the passthrough itself.
  [ -x "$B/wbrstm" ] || { sk "BRSTM ADPCM_THP encode/decode roundtrip"; return; }
  local d; d=$(mktemp -d)
  python3 -c "
import struct, math
sr=32000; n=8000
data=b''.join(struct.pack('<h', int(6000*math.sin(i*0.05))) for i in range(n))
hdr=b'RIFF'+struct.pack('<I',36+len(data))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,sr,sr*2,2,16)+b'data'+struct.pack('<I',len(data))
open('$d/in.wav','wb').write(hdr+data)
" || { no "BRSTM ADPCM_THP encode/decode roundtrip" "couldn't synthesize input WAV"; rm -rf "$d"; return; }

  local ok=1
  "$B/wbrstm" from_wav "$d/in.wav" "$d/out.brstm" >"$d/encode.log" 2>&1 || ok=0
  [ -s "$d/out.brstm" ] || ok=0
  "$B/wbrstm" to_wav "$d/out.brstm" "$d/roundtrip.wav" >/dev/null 2>&1 || ok=0
  [ -s "$d/roundtrip.wav" ] || ok=0
  grep -q "ADPCM_THP" <("$B/wbrstm" info "$d/out.brstm" 2>&1) || ok=0

  rm -rf "$d"
  [ "$ok" = 1 ] && ok "BRSTM ADPCM_THP encode/decode roundtrip" \
    || no "BRSTM ADPCM_THP encode/decode roundtrip" "encode, decode, or info failed"
}
t_brstm_roundtrip

t_extex(){
  # Monster Games (Excite Truck / ExciteBots, Wii) .tex GX textures: no
  # magic, so found by extension over SEARCH+extra Excite sample roots and
  # confirmed by successful decode (dimensions + pixel format recovered
  # purely from the mip-chain-consistency heuristic, no stored format field).
  local f; f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f -iname '*.tex' -size -65M \
        ! -path '*claude-*' ! -iname 'test.*' ! -iname 'test_*' 2>/dev/null
    done | head -1)
  [ -n "$f" ] || { sk "Excite .tex GX texture"; return; }
  rm -rf /tmp/_r_extex; mkdir -p /tmp/_r_extex
  cp "$f" /tmp/_r_extex/
  $B/wszst EXTRACT "/tmp/_r_extex/$(basename "$f")" --overwrite >/tmp/_r_extex.log 2>&1
  local png="/tmp/_r_extex/$(basename "${f%.*}").png"
  [ -s "$png" ] && grep -q "EXTRACT TEX:" /tmp/_r_extex.log \
    && ok "Excite .tex GX texture -> PNG ($f)" \
    || no "Excite .tex GX texture" "$f"
}
t_extex

t_gtx(){
  # Wii U GX2 "Gfx2" texture container: standalone .gtx files (e.g. debug
  # fonts, UI textures shipped outside any BFRES) were previously not
  # recognised by `wszst EXTRACT --decode`'s image-detection list even
  # though the underlying DecodeGTX_RGBA codec already worked fine via
  # wimgt -- fixed by adding the Gfx2 magic/.gtx extension check alongside
  # the other image formats in decode_image_if_possible().
  local f="$PWD_PROJECT/../tests/fixtures/wiiu_debug_font.gtx"
  [ -f "$f" ] || { sk "Wii U GTX standalone texture"; return; }
  rm -rf /tmp/_r_gtx; mkdir -p /tmp/_r_gtx
  cp "$f" /tmp/_r_gtx/
  $B/wszst EXTRACT "/tmp/_r_gtx/$(basename "$f")" --decode --overwrite >/tmp/_r_gtx.log 2>&1
  local png="/tmp/_r_gtx/$(basename "$f").png"
  [ -s "$png" ] && grep -q "DECODE .*-> PNG:.*\.gtx\.png" /tmp/_r_gtx.log \
    && ok "Wii U GTX standalone texture -> PNG" \
    || no "Wii U GTX standalone texture" "$f"
}
t_gtx

t_gtx_encode(){
  # GTX encode (wimgt CONVERT -> .gtx) round trip: decode a real sample to
  # PNG, re-encode it, decode the re-encode, and require pixel-identical
  # PNG output -- i.e. the encoder's 2D macro-tile addressing is a verified
  # exact inverse of the decoder, not just "looks plausible".
  local f="$PWD_PROJECT/../tests/fixtures/wiiu_debug_font.gtx"
  [ -f "$f" ] || { sk "Wii U GTX encode round trip"; return; }
  rm -rf /tmp/_r_gtx_enc; mkdir -p /tmp/_r_gtx_enc
  $B/wimgt DECODE "$f" --dest /tmp/_r_gtx_enc/orig.png --overwrite >/tmp/_r_gtx_enc.log 2>&1
  $B/wimgt CONVERT /tmp/_r_gtx_enc/orig.png --dest /tmp/_r_gtx_enc/reenc.gtx --overwrite >>/tmp/_r_gtx_enc.log 2>&1
  $B/wimgt DECODE /tmp/_r_gtx_enc/reenc.gtx --dest /tmp/_r_gtx_enc/reenc.png --overwrite >>/tmp/_r_gtx_enc.log 2>&1
  cmp -s /tmp/_r_gtx_enc/orig.png /tmp/_r_gtx_enc/reenc.png \
    && ok "Wii U GTX encode round trip (pixel-identical)" \
    || no "Wii U GTX encode round trip" "$f"
}
t_gtx_encode

t_exart(){
  # Monster Games .art/.img GUI images: same GX pixel data as .tex but a
  # single mip level with a zeroed footer, so dimensions/format come from
  # tile-seam continuity alone. Colour+stencil pairs are detected and
  # recombined into one proper RGBA image by ScanART -- see t_exart_mask.
  local f; f=$(for d in $SEARCH; do [ -d "$d" ] || continue
      find -L "$d" -maxdepth 8 -type f \( -iname '*.art' -o -iname '*.img' \) -size -65M \
        ! -path '*claude-*' ! -iname 'test.*' ! -iname 'test_*' 2>/dev/null
    done | head -1)
  [ -n "$f" ] || { sk "Excite .art/.img GUI image"; return; }
  rm -rf /tmp/_r_exart; mkdir -p /tmp/_r_exart
  cp "$f" /tmp/_r_exart/
  $B/wszst EXTRACT "/tmp/_r_exart/$(basename "$f")" --overwrite >/tmp/_r_exart.log 2>&1
  local png="/tmp/_r_exart/$(basename "${f%.*}").png"
  [ -s "$png" ] && grep -q "EXTRACT ART:" /tmp/_r_exart.log \
    && ok "Excite .art/.img GUI image -> PNG ($f)" \
    || no "Excite .art/.img GUI image" "$f"
}
t_exart

t_exart_mask(){
  # Some real .art files are a colour+stencil pair: the raw GX decode comes
  # out as one image twice its real height (colour on top, stencil mask
  # below). ScanART() must detect that shape and recombine it into one real
  # half-height RGBA image (colour + a genuine cutout alpha channel) instead
  # of emitting the stacked buffer as-is -- see excite_art_looks_like_mask()/
  # excite_art_recombine() in lib-excite.c.
  for spec in "excite_ach_trun:128" "excite_silvcoin:256"; do
    local name="${spec%%:*}" want="${spec##*:}"
    local f="$PWD_PROJECT/../tests/fixtures/$name.art"
    [ -f "$f" ] || { sk "Excite .art colour+stencil recombine ($name)"; continue; }
    rm -rf /tmp/_r_exartm; mkdir -p /tmp/_r_exartm
    cp "$f" /tmp/_r_exartm/
    $B/wszst EXTRACT "/tmp/_r_exartm/$name.art" --overwrite >/tmp/_r_exartm.log 2>&1
    local png="/tmp/_r_exartm/$name.png"
    if [ -s "$png" ] && grep -qE "EXTRACT ART:.*\(${want}x${want}\)" /tmp/_r_exartm.log; then
      if python3 - "$png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
alphas = [p[3] for p in im.getdata()]
opaque = sum(1 for a in alphas if a > 200)
transparent = sum(1 for a in alphas if a < 55)
# a real cutout has meaningful amounts of both, not one flat channel
sys.exit(0 if opaque > len(alphas)*0.05 and transparent > len(alphas)*0.05 else 1)
PY
      then
        ok "Excite .art colour+stencil recombine -> ${want}x${want} RGBA ($name)"
      else
        no "Excite .art colour+stencil recombine" "$name: no real alpha cutout"
      fi
    else
      no "Excite .art colour+stencil recombine" "$name"
    fi
  done
}
t_exart_mask

t_extex_encode(){
  # Excite .tex encode (wimgt CONVERT PNG -> .etex, the CLI destination
  # extension for the Excite TEX encoder -- see wimgt.c's cmd_convert(); a
  # collision with the pre-existing BRRES TEX0 encoder's own .tex output
  # ruled out reusing that extension). Neither GX format nor mip-count is
  # stored in the file, so the encoder self-verifies by decoding its own
  # output back through the real ScanTEX() classifier and only accepts a
  # candidate whose recovered width/height/format *and* decoded pixels
  # match what was requested -- verified against 3,151 decodable retail
  # .tex files: 2,723 round-trip exactly or within a tight per-pixel
  # tolerance, the rest fail loudly (ERR_INVALID_DATA) rather than risk a
  # silently wrong image, since many (format,width,height,level-count)
  # combinations can alias to the same file size with no header to
  # disambiguate them.
  local f="$PWD_PROJECT/../tests/fixtures/excite_bat_d2.tex"
  [ -f "$f" ] || { sk "Excite .tex encode round trip"; return; }
  rm -rf /tmp/_r_extexenc; mkdir -p /tmp/_r_extexenc
  cp "$f" /tmp/_r_extexenc/orig.tex
  $B/wszst EXTRACT /tmp/_r_extexenc/orig.tex --dest /tmp/_r_extexenc/orig.png -o >/tmp/_r_extexenc.log 2>&1
  $B/wimgt CONVERT /tmp/_r_extexenc/orig.png --dest /tmp/_r_extexenc/re.etex -o >>/tmp/_r_extexenc.log 2>&1
  cp /tmp/_r_extexenc/re.etex /tmp/_r_extexenc/re.tex
  $B/wszst EXTRACT /tmp/_r_extexenc/re.tex --dest /tmp/_r_extexenc/re.png -o >>/tmp/_r_extexenc.log 2>&1
  if [ -s /tmp/_r_extexenc/re.png ] && python3 - /tmp/_r_extexenc/orig.png /tmp/_r_extexenc/re.png <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGBA")
b = Image.open(sys.argv[2]).convert("RGBA")
sys.exit(0 if a.size == b.size and list(a.getdata()) == list(b.getdata()) else 1)
PY
  then
    ok "Excite .tex encode round trip (exact pixel match)"
  else
    no "Excite .tex encode round trip" "see /tmp/_r_extexenc.log"
  fi
}
t_extex_encode

t_exart_encode(){
  # Excite .art/.img encode (wimgt CONVERT PNG -> .art), same self-verify
  # contract as t_extex_encode() but for the single-mip-level GUI image
  # format -- verified against 94 decodable retail .art files: 84 round-
  # trip exactly or within tolerance, the rest fail loudly rather than
  # ship a wrong image.
  for name in excite_ach_trun excite_silvcoin; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.art"
    [ -f "$f" ] || { sk "Excite .art encode round trip ($name)"; continue; }
    rm -rf /tmp/_r_exartenc; mkdir -p /tmp/_r_exartenc
    cp "$f" /tmp/_r_exartenc/orig.art
    $B/wszst EXTRACT /tmp/_r_exartenc/orig.art --dest /tmp/_r_exartenc/orig.png -o >/tmp/_r_exartenc.log 2>&1
    $B/wimgt CONVERT /tmp/_r_exartenc/orig.png --dest /tmp/_r_exartenc/re.art -o >>/tmp/_r_exartenc.log 2>&1
    $B/wszst EXTRACT /tmp/_r_exartenc/re.art --dest /tmp/_r_exartenc/re.png -o >>/tmp/_r_exartenc.log 2>&1
    if [ -s /tmp/_r_exartenc/re.png ] && python3 - /tmp/_r_exartenc/orig.png /tmp/_r_exartenc/re.png <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGBA")
b = Image.open(sys.argv[2]).convert("RGBA")
sys.exit(0 if a.size == b.size and list(a.getdata()) == list(b.getdata()) else 1)
PY
    then
      ok "Excite .art encode round trip (exact pixel match, $name)"
    else
      no "Excite .art encode round trip" "$name: see /tmp/_r_exartenc.log"
    fi
  done
}
t_exart_encode

t_exmsh(){
  # Monster Games PMsh collision resources: count header, spatial buckets,
  # indexed float32 positions and 60-byte triangle/collision records -> DAE.
  # Counts are pinned from retail resources so this verifies topology rather
  # than merely checking that a non-empty file was produced.
  local spec name want_pos want_tri
  for spec in "excite_goalback 16 16" "excite_gpmesh 221 248" "excite_rail2bp 222 198"; do
    read -r name want_pos want_tri <<<"$spec"
    local f="$PWD_PROJECT/../tests/fixtures/$name.msh"
    [ -f "$f" ] || { sk "Excite PMsh collision mesh ($name)"; continue; }
    rm -rf /tmp/_r_exmsh; mkdir -p /tmp/_r_exmsh
    cp "$f" /tmp/_r_exmsh/
    $B/wszst EXTRACT "/tmp/_r_exmsh/$name.msh" --overwrite >/tmp/_r_exmsh.log 2>&1
    local dae="/tmp/_r_exmsh/$name.dae"
    if [ -s "$dae" ] && grep -q "EXTRACT MSH:" /tmp/_r_exmsh.log; then
      local nfloat ntri
      nfloat=$(grep -oE '<float_array[^>]*count="[0-9]+"' "$dae" | head -1 | grep -oE '[0-9]+')
      ntri=$(grep -oE '<triangles[^>]*count="[0-9]+"' "$dae" | head -1 | grep -oE '[0-9]+')
      if [ "$nfloat" = "$((want_pos*3))" ] && [ "$ntri" = "$want_tri" ]; then
        ok "Excite PMsh collision mesh -> DAE ($name: $want_pos positions, $want_tri tris)"
      else
        no "Excite PMsh collision mesh" "$name: expected $((want_pos*3)) position floats/$want_tri tris, got $nfloat/$ntri"
      fi
    else
      no "Excite PMsh collision mesh" "$name"
    fi
  done
}
t_exmsh

echo "== HAL HSFV037 model geometry (Mario Party 4-8 .hsf) =="
t_hsf(){
  # Single-part case, using the AttributeHeader table generalised to
  # length 1 -- see lib-hsf.c. The fixture is a synthetic unit cube
  # (8 verts, 12 tris, named "cube_vtxs"/"cube_nrms"/"cube_faces" in its
  # own string table) used to confirm the section layout against ground
  # truth, not an extracted retail asset.
  local f="$PWD_PROJECT/../tests/fixtures/hsf_cube_test.hsf"
  [ -f "$f" ] || { sk "HSF single-part geometry"; return; }
  rm -rf /tmp/_r_hsf; mkdir -p /tmp/_r_hsf
  cp "$f" /tmp/_r_hsf/
  $B/wszst EXTRACT "/tmp/_r_hsf/$(basename "$f")" --overwrite >/tmp/_r_hsf.log 2>&1
  local glb="/tmp/_r_hsf/${f##*/}"; glb="${glb%.*}.glb"
  if [ -s "$glb" ] && grep -q "EXTRACT HSF:" /tmp/_r_hsf.log \
      && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
    local nv; nv=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
acc = g.json["accessors"][g.json["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
print(acc["count"])
PY
)
    # GLB export is unindexed triangle-soup (unlike DAE's shared-vertex
    # accessors), so a 12-triangle cube yields 36 corner vertices, not 8.
    [ "$nv" = "36" ] && ok "HSF cube -> GLB (36 verts / 12 tris, validated)" \
      || no "HSF cube -> GLB" "unexpected vertex count $nv in $glb"
  else
    no "HSF cube -> GLB" "$f"
  fi
}
t_hsf

t_hsf_multipart(){
  # Real retail multi-part sample: a Mario Party 4 board-piece .hsf (2
  # named mesh parts, "cyl1" and "player") extracted from a legitimately-
  # owned disc dump in a prior session. Exercises the AttributeHeader
  # array (count>1) path, per-mesh-name attribute matching, and the
  # packed-s8-normal detection -- see lib-hsf.c.
  local f="$PWD_PROJECT/../tests/fixtures/hsf_multipart_test.hsf"
  [ -f "$f" ] || { sk "HSF multi-part geometry"; return; }
  rm -rf /tmp/_r_hsfmp; mkdir -p /tmp/_r_hsfmp
  cp "$f" /tmp/_r_hsfmp/
  $B/wszst EXTRACT "/tmp/_r_hsfmp/$(basename "$f")" --overwrite >/tmp/_r_hsfmp.log 2>&1
  local glb="/tmp/_r_hsfmp/${f##*/}"; glb="${glb%.*}.glb"
  if [ -s "$glb" ] && grep -q "EXTRACT HSF:" /tmp/_r_hsfmp.log \
      && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
    local stats; stats=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
print(len(g.json.get("meshes", [])),len(g.json.get("materials", [])),len(g.json.get("images", [])),len(g.json.get("nodes", [])))
PY
)
    set -- $stats
    [ "$1" = "2" ] && [ "$2" -gt 0 ] && [ "$4" -gt 2 ] \
      && [ -s /tmp/_r_hsfmp/miraco.png ] \
      && ok "HSF board-piece -> textured hierarchical GLB (2 meshes, validated)" \
      || no "HSF board-piece -> GLB" "expected meshes/materials/images/nodes + miraco.png, got $stats"
  else
    no "HSF board-piece -> GLB" "$f"
  fi
}
t_hsf_multipart

echo "== Monster Games NDL3/NDL2 model geometry (Excite Truck / ExciteBots .mod) =="
t_exmod(){
  # DecodeExciteMOD() reads geometry format straight out of the embedded GX
  # display list's vertex-attribute-table register writes rather than
  # hardcoding a single validated shape -- see the long comment above
  # DecodeExciteMOD() in lib-excite.c for the format and its validation
  # against the full retail corpus of both games (135/135 Excite Truck,
  # 193/203 ExciteBots -- the ExciteBots gap is the separate .msh files,
  # not .mod decode failures). These 3 small fixtures (all "3LDN", the
  # magic not at file offset 0 on 2 of them, matching most real samples)
  # exercise that general path, not a narrow special case.
  for name in excite_arrow_obj excite_arrow_point excite_sunflower2; do
    local f="$PWD_PROJECT/../tests/fixtures/$name.mod"
    [ -f "$f" ] || { sk "NDL3 .mod ($name)"; continue; }
    rm -rf /tmp/_r_exmod; mkdir -p /tmp/_r_exmod
    cp "$f" /tmp/_r_exmod/
    $B/wszst EXTRACT "/tmp/_r_exmod/$name.mod" --overwrite >/tmp/_r_exmod.log 2>&1
    local glb="/tmp/_r_exmod/$name.glb"
    if [ -s "$glb" ] && grep -q "EXTRACT MOD:" /tmp/_r_exmod.log \
        && python3 "$PWD_PROJECT/../tests/validate-glb.py" "$glb" >/dev/null 2>&1; then
      local nv; nv=$(python3 - "$glb" <<'PY'
import sys
from importlib.util import spec_from_file_location, module_from_spec
spec = spec_from_file_location("vglb", "../tests/validate-glb.py")
m = module_from_spec(spec); spec.loader.exec_module(m)
g = m.load_glb(sys.argv[1])
acc = g.json["accessors"][g.json["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
print(acc["count"])
PY
)
      [ "$nv" -gt 0 ] 2>/dev/null && ok "NDL3 .mod -> GLB ($name, $nv verts, validated)" \
        || no "NDL3 .mod -> GLB" "$name: bad vertex count $nv"
    else
      no "NDL3 .mod -> GLB" "$name"
    fi
  done
}
t_exmod

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
