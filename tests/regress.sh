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
  find -L "$d" -maxdepth 8 -type f -size -65M \( \
      -iname '*.bch' -o -iname '*.bcres' -o -iname '*.cgfx' -o -iname '*.nsbmd' \
      -o -iname '*.bfres' -o -iname '*.bntx' -o -iname '*.bmd' \
      -o -iname '*.plt0' -o -iname '*.pac' -o -iname '*.gfa' -o -iname '*.brfnt' \
      -o -iname '*.brfna' -o -iname '*.ctpk' \
      -o -iname '*.byml' -o -iname '*.byaml' -o -iname '*.narc' \
      -o -iname '*.brsar' -o -iname '*.bffnt' -o -iname '*.bcfnt' \
      -o -iname '*.brlan' -o -iname '*.brlyt' \) 2>/dev/null
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
    local g=$(grep -c '<geometry' /tmp/_r.dae 2>/dev/null||echo 0)
    if [ "$g" -gt 0 ]; then
      ok "$1 -> DAE ($g geometries)"
      found=1
      break
    fi
  done < <(awk -F'\t' -v m="$2" '$1==m{print $2}' "$IDX")
  [ "$found" -eq 1 ] || {
    local first; first=$(find_magic "$2")
    [ -n "$first" ] && no "$1 -> DAE" "no geometry from $first" || sk "$1"
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
  # PLAN.md SS8 for the wider naming-convention picture. MDL0 sampler links
  # are preferred when present; constrained retail naming fallbacks cover
  # archives that leave those offsets unresolved.
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

  # BRRES puts MDL0 files under 3DModels(NW4R), while decoded TEX0 images
  # live in sibling Textures(NW4R).  A relative init_from that walks across
  # that boundary is valid COLLADA, but many importers -- macOS Preview among
  # them -- only look for the base name in the document's own directory, so
  # the exporter materialises each referenced image beside the .dae and
  # references it by bare name. Assert both halves: the reference and the
  # payload actually landing next to the model.
  rm -rf /tmp/_r_brres_dae; mkdir -p /tmp/_r_brres_dae
  $B/wszst XX "$f" --dest /tmp/_r_brres_dae --overwrite >/tmp/_r_brres_dae.log 2>&1
  local dae="/tmp/_r_brres_dae/3DModels(NW4R)/ins_taran.dae"
  local mdl0="/tmp/_r_brres_dae/3DModels(NW4R)/ins_taran"
  local geometry triangles positions normals texcoords bound images instances
  geometry=$(grep -c '<geometry ' "$dae" 2>/dev/null || echo 0)
  triangles=$(grep -c '<triangles count="[1-9][0-9]*"' "$dae" 2>/dev/null || echo 0)
  # Geometry/controller ids are the MDL0's own object names, and material
  # symbols are the material names -- BrawlCrate's convention, and the only
  # way an exported DAE can be matched back to the archive it came from.
  positions=$(grep -c '<float_array id="polygon[01]-positions-array" count="[1-9][0-9]*"' "$dae" 2>/dev/null || echo 0)
  normals=$(grep -c '<float_array id="polygon[01]-normals-array" count="[1-9][0-9]*"' "$dae" 2>/dev/null || echo 0)
  texcoords=$(grep -c '<float_array id="polygon[01]-texcoords-array" count="[1-9][0-9]*"' "$dae" 2>/dev/null || echo 0)
  bound=$(grep -c '<triangles count="[1-9][0-9]*" material="m[01]"' "$dae" 2>/dev/null || echo 0)
  images=$(grep -c '<image .*name="ins_taran"' "$dae" 2>/dev/null || echo 0)
  instances=$(grep -c '<instance_material symbol="m[01]"' "$dae" 2>/dev/null || echo 0)
  local controllers skinned
  controllers=$(grep -c '<controller id="polygon[01]-skin"' "$dae" 2>/dev/null || echo 0)
  skinned=$(grep -c '<instance_controller url="#polygon[01]-skin"' "$dae" 2>/dev/null || echo 0)
  local xml_ok=1 assimp_ok=1 semantic_ok=1
  if command -v xmllint >/dev/null && ! xmllint --noout "$dae" >/dev/null 2>&1; then xml_ok=0; fi
  if command -v assimp >/dev/null && ! assimp info "$dae" >/dev/null 2>&1; then assimp_ok=0; fi
  if ! python3 "$DAE_VALIDATOR" --require-images "$dae" >/dev/null 2>&1; then semantic_ok=0; fi
  if [ -s "$dae" ] && [ "$geometry" -gt 0 ] \
      && [ "$triangles" -eq "$geometry" ] \
      && [ "$positions" -eq "$geometry" ] \
      && [ "$normals" -eq "$geometry" ] \
      && [ "$texcoords" -eq "$geometry" ] \
      && [ "$bound" -eq "$geometry" ] \
      && [ "$instances" -eq "$geometry" ] \
      && [ "$images" -eq 2 ] \
      && [ "$controllers" -eq "$geometry" ] && [ "$skinned" -eq "$geometry" ] \
      && [ "$xml_ok" -eq 1 ] && [ "$assimp_ok" -eq 1 ] && [ "$semantic_ok" -eq 1 ] \
      && ! grep -q '<node id=""' "$dae" \
      && grep -q '0.128204 0.222473' "$dae" \
      && grep -q '<geometry id="polygon0" name="polygon0">' "$dae" \
      && grep -q '<bind_vertex_input semantic="TEXCOORD0" input_semantic="TEXCOORD" input_set="0"' "$dae" \
      && grep -q '<init_from>ins_taran.png</init_from>' "$dae" \
      && [ -s "/tmp/_r_brres_dae/3DModels(NW4R)/ins_taran.png" ]; then
    ok "BRRES MDL0 geometry + material/UV/texture mapping ($geometry meshes)"
  else
    no "BRRES MDL0 geometry + material/UV/texture mapping" "$f"
  fi

  # A directory supplied directly to XX must take the same guarded recursive
  # path as a pass-through extractor's staging directory. Use mktemp rather
  # than a fixed directory so this test never consumes or removes user files.
  local tree
  tree=$(mktemp -d /tmp/_r_xx_dir.XXXXXX) || { no "XX directory recursion" "mktemp failed"; return; }
  cp "$f" "$tree/accf_ins_taran.brres"
  $B/wszst XX "$tree" --overwrite >/tmp/_r_xx_dir.log 2>&1
  dae="$tree/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.dae"
  mdl0="$tree/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran"
  if [ -s "$dae" ] && grep -q '<triangles count="[1-9][0-9]*"' "$dae" \
      && grep -q '<init_from>ins_taran.png</init_from>' "$dae" \
      && [ -s "$tree/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.png" ]; then
    ok "XX directory recursion -> nested BRRES DAE"
  else
    no "XX directory recursion" "$tree"
  fi

  # A pass-through disc extractor supplies both a completed staging directory
  # and the user's --dest. The final model pass must keep each DAE beside its
  # MDL0; applying --dest again collapses thousands of same-named resources
  # into one flat directory (and historically stripped the .dae suffix).
  local staged
  staged=$(mktemp -d /tmp/_r_xx_dest.XXXXXX) || { no "XX staged tree with --dest" "mktemp failed"; return; }
  mkdir -p "$staged/source/nested" "$staged/destination"
  cp "$f" "$staged/source/nested/accf_ins_taran.brres"
  $B/wszst XX "$staged/source" --dest "$staged/destination" --overwrite >/dev/null 2>&1
  dae="$staged/source/nested/accf_ins_taran.brres.d/3DModels(NW4R)/ins_taran.dae"
  if [ -s "$dae" ] && grep -q '<triangles count="[1-9][0-9]*"' "$dae" \
      && [ ! -e "$staged/destination/ins_taran" ]; then
    ok "XX staged tree + --dest -> preserves nested DAE path"
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
  local sample_dir="$HOME/Downloads/wszst-samples"
  local spider_src="$sample_dir/accf_ins_taran.brres"
  local npc_src="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$spider_src" ] || [ ! -f "$npc_src" ]; then
    sk "ACCF MDL0 bind-pose transforms"; return
  fi
  local out
  out=$(mktemp -d /tmp/_r_accf_bind.XXXXXX) || { no "ACCF MDL0 bind-pose transforms" "mktemp failed"; return; }
  $B/wszst XX "$spider_src" --dest "$out/spider" --overwrite >/dev/null 2>&1
  $B/wszst XX "$npc_src" --dest "$out/npc" --overwrite >/dev/null 2>&1
  local spider="$out/spider/3DModels(NW4R)/ins_taran.dae"
  local npc="$out/npc/3DModels(NW4R)/tti.dae"
  if python3 - "$spider" "$npc" <<'PY'
import sys
import xml.etree.ElementTree as ET

ns = "{http://www.collada.org/2005/11/COLLADASchema}"

def inspect(path):
    root = ET.parse(path).getroot()
    points = []
    for array in root.iter(ns + "float_array"):
        if array.get("id", "").endswith("-positions-array"):
            values = [float(x) for x in (array.text or "").split()]
            points.extend(zip(values[0::3], values[1::3], values[2::3]))
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    joints = {node.get("name") for node in root.iter(ns + "node")
              if node.get("type") == "JOINT"}
    unit = root.find("./" + ns + "asset/" + ns + "unit")
    assert unit is not None and unit.get("meter") == "0.01"
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
  local src="$HOME/Downloads/wszst-samples/accf_ins_mukade.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 NodeMix transforms"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_nodemix.XXXXXX) || { no "ACCF MDL0 NodeMix transforms" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out/model" --overwrite >/dev/null 2>&1
  local dae="$out/model/3DModels(NW4R)/ins_mukade.dae"
  if python3 "$DAE_VALIDATOR" --require-images "$dae" >/dev/null 2>&1 \
      && { ! command -v assimp >/dev/null || assimp info "$dae" >/dev/null 2>&1; } \
      && python3 - "$dae" <<'PY'
import sys
import xml.etree.ElementTree as ET
n = "{http://www.collada.org/2005/11/COLLADASchema}"
root = ET.parse(sys.argv[1]).getroot()
points = []
for array in root.iter(n + "float_array"):
    if array.get("id", "").endswith("-positions-array"):
        values = [float(x) for x in (array.text or "").split()]
        points.extend(zip(values[0::3], values[1::3], values[2::3]))
low = [min(p[i] for p in points) for i in range(3)]
high = [max(p[i] for p in points) for i in range(3)]
joints = {node.get("name") for node in root.iter(n + "node")
          if node.get("type") == "JOINT"}
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
  local sample_dir="$HOME/Downloads/wszst-samples"
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
  local sample_dir="$HOME/Downloads/wszst-samples"
  local model="$sample_dir/accf_000_0.brres"
  local pack="$sample_dir/accf_pat0season00.brres"
  if [ ! -f "$model" ] || [ ! -f "$pack" ]; then sk "ACCF shared BRRES texture tree"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_shared.XXXXXX) || { no "ACCF shared BRRES texture tree" "mktemp failed"; return; }
  mkdir -p "$tree/BgData/BgModel" "$tree/BgData/Pack"
  cp "$model" "$tree/BgData/BgModel/"
  cp "$pack" "$tree/BgData/Pack/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local dae="$tree/BgData/BgModel/accf_000_0.brres.d/3DModels(NW4R)/grd_Ce_0.dae"
  local grass="$tree/BgData/Pack/accf_pat0season00.brres.d/Textures(NW4R)/tex_grass.png"
  # The cross-archive case is exactly where a self-contained model matters
  # most: the resolved image lives several directories away, so it is
  # materialised beside the .dae and referenced by bare name.
  local local_grass="$tree/BgData/BgModel/accf_000_0.brres.d/3DModels(NW4R)/tex_grass.png"
  if [ -s "$dae" ] && [ -s "$grass" ] && [ -s "$local_grass" ] \
      && cmp -s "$grass" "$local_grass" \
      && grep -q '<init_from>tex_grass.png</init_from>' "$dae" \
      && python3 "$DAE_VALIDATOR" --require-images "$dae" >/dev/null 2>&1 \
      && { ! command -v assimp >/dev/null || assimp info "$dae" >/dev/null 2>&1; }; then
    ok "ACCF shared BRRES texture tree -> resolved DAE image"
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
  local sample_dir="$HOME/Downloads/wszst-samples"
  local model="$sample_dir/accf_excap0.brres"
  local npc="$sample_dir/accf_npc_special_13.brres"
  if [ ! -f "$model" ] || [ ! -f "$npc" ]; then sk "ACCF v9 material texture links"; return; fi
  local tree
  tree=$(mktemp -d /tmp/_r_accf_v9.XXXXXX) || { no "ACCF v9 material texture links" "mktemp failed"; return; }
  mkdir -p "$tree/Item/Excap" "$tree/Npc/Special"
  cp "$model" "$tree/Item/Excap/"; cp "$npc" "$tree/Npc/Special/"
  $B/wszst XX "$tree" --overwrite >"$tree.log" 2>&1
  local dae="$tree/Item/Excap/accf_excap0.brres.d/3DModels(NW4R)/excap998.dae"
  local images materials geometry
  images=$(grep -c '<image ' "$dae" 2>/dev/null || echo 0)
  materials=$(grep -c '<material ' "$dae" 2>/dev/null || echo 0)
  geometry=$(grep -c '<geometry ' "$dae" 2>/dev/null || echo 0)
  if [ "$images" -eq 2 ] && [ "$materials" -eq 4 ] && [ "$geometry" -eq 4 ] \
      && grep -q 'name="excap998_0"' "$dae" \
      && grep -q 'name="h"' "$dae" \
      && ! grep -q 'name="e.0"\|name="m.0"\|Npc/Special' "$dae" \
      && python3 "$DAE_VALIDATOR" --require-images "$dae" >/dev/null 2>&1; then
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
  local src="$HOME/Downloads/wszst-samples/accf_bone_only_229.brres"
  [ -f "$src" ] || { sk "ACCF bone-only MDL0 -> DAE"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_empty.XXXXXX) || { no "ACCF bone-only MDL0 -> DAE" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local dae="$out/3DModels(NW4R)/idr_ms_pictureA2.dae"
  if [ -s "$dae" ] && grep -q 'name="idr_ms_pictureA2" sid="idr_ms_pictureA2" type="JOINT"' "$dae" \
      && ! grep -q '<geometry ' "$dae" \
      && { ! command -v xmllint >/dev/null || xmllint --noout "$dae" >/dev/null 2>&1; }; then
    ok "ACCF bone-only MDL0 -> skeleton DAE"
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
  local dae="$out/3DModels(NW4R)/umb_md.dae"
  if [ -s "$dae" ] && python3 - "$dae" <<'SKINPY' >/dev/null 2>&1
import math, sys
import xml.etree.ElementTree as ET

NS = "{http://www.collada.org/2005/11/COLLADASchema}"


def ident():
    return [[float(r == c) for c in range(4)] for r in range(4)]


def mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def rot(axis, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    m = ident()
    if axis == 0:
        m[1][1], m[1][2], m[2][1], m[2][2] = c, -s, s, c
    elif axis == 1:
        m[0][0], m[0][2], m[2][0], m[2][2] = c, s, -s, c
    else:
        m[0][0], m[0][1], m[1][0], m[1][1] = c, -s, s, c
    return m


def local(node):
    m = ident()
    for child in node:
        tag = child.tag[len(NS):]
        values = [float(v) for v in (child.text or "").split()]
        if tag == "translate":
            t = ident()
            for i in range(3):
                t[i][3] = values[i]
            m = mul(m, t)
        elif tag == "rotate":
            axis = 0 if values[0] else 1 if values[1] else 2
            m = mul(m, rot(axis, values[3]))
        elif tag == "scale":
            t = ident()
            for i in range(3):
                t[i][i] = values[i]
            m = mul(m, t)
        elif tag == "matrix":
            m = mul(m, [values[r * 4:r * 4 + 4] for r in range(4)])
    return m


root = ET.parse(sys.argv[1]).getroot()
world = {}


def walk(node, parent):
    here = mul(parent, local(node))
    if node.get("id"):
        world[node.get("id")] = here
    for child in node.findall(NS + "node"):
        walk(child, here)


scene = root.find(".//" + NS + "visual_scene")
for node in scene.findall(NS + "node"):
    walk(node, ident())

controllers = 0
for controller in root.iter(NS + "controller"):
    skin = controller.find(NS + "skin")
    sources = {}
    for source in skin.findall(NS + "source"):
        names = source.find(NS + "Name_array")
        floats = source.find(NS + "float_array")
        sources[source.get("id")] = (names if names is not None else floats).text.split()
    joints = skin.find(NS + "joints")
    inputs = {i.get("semantic"): i.get("source")[1:] for i in joints}
    names = sources[inputs["JOINT"]]
    raw = [float(v) for v in sources[inputs["INV_BIND_MATRIX"]]]
    assert len(raw) == len(names) * 16, "inverse bind matrix count"
    for index, name in enumerate(names):
        inverse = [raw[index * 16 + r * 4:index * 16 + r * 4 + 4] for r in range(4)]
        product = mul(world[name], inverse)
        for r in range(4):
            for c in range(4):
                assert abs(product[r][c] - (1.0 if r == c else 0.0)) < 2e-3, (name, r, c)
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
  local src="$HOME/Downloads/wszst-samples/accf_ins_taran.brres"
  [ -f "$src" ] || { sk "ACCF MDL0 face winding"; return; }
  local out
  out=$(mktemp -d /tmp/_r_accf_wind.XXXXXX) || { no "ACCF MDL0 face winding" "mktemp failed"; return; }
  $B/wszst XX "$src" --dest "$out" --overwrite >/dev/null 2>&1
  local dae="$out/3DModels(NW4R)/ins_taran.dae"
  if [ -s "$dae" ] && python3 - "$dae" <<'WINDPY' >/dev/null 2>&1
import sys
import xml.etree.ElementTree as ET

NS = "{http://www.collada.org/2005/11/COLLADASchema}"


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


root = ET.parse(sys.argv[1]).getroot()
agree = disagree = 0
for geometry in root.iter(NS + "geometry"):
    mesh = geometry.find(NS + "mesh")
    sources = {}
    for source in mesh.findall(NS + "source"):
        stride = int(source.find(".//" + NS + "accessor").get("stride"))
        values = [float(v) for v in source.find(NS + "float_array").text.split()]
        sources[source.get("id")] = [values[i:i + stride]
                                     for i in range(0, len(values), stride)]
    position_source = mesh.find(NS + "vertices").find(NS + "input").get("source")[1:]
    triangles = mesh.find(NS + "triangles")
    if triangles is None:
        continue
    inputs = {}
    for entry in triangles.findall(NS + "input"):
        key = entry.get("semantic") + (entry.get("set") or "")
        inputs.setdefault(key, (int(entry.get("offset")), entry.get("source")[1:]))
    if "NORMAL" not in inputs:
        continue
    width = max(offset for offset, _ in inputs.values()) + 1
    indices = [int(v) for v in triangles.find(NS + "p").text.split()]
    positions = sources[position_source]
    normals = sources[inputs["NORMAL"][1]]
    position_offset = inputs["VERTEX"][0]
    normal_offset = inputs["NORMAL"][0]
    for base in range(0, len(indices), width * 3):
        corner = [indices[base + i * width + position_offset] for i in range(3)]
        shading = [indices[base + i * width + normal_offset] for i in range(3)]
        p0, p1, p2 = (positions[i] for i in corner)
        e1 = [p1[i] - p0[i] for i in range(3)]
        e2 = [p2[i] - p0[i] for i in range(3)]
        geometric = cross(e1, e2)
        authored = [sum(normals[i][axis] for i in shading) / 3.0 for axis in range(3)]
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
  local src="$HOME/Downloads/wszst-samples/accf_breft_indexed.bt-img"
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

echo "== compression round-trips =="
# The compression format is chosen by the DESTINATION EXTENSION, not a flag.
printf 'The quick brown fox jumps over the lazy dog. %.0s' {1..400} > /tmp/_r.bin
for e in lz10 lz11 rl yay0 ash0 lzh8 qlz at7 blz huff4 huff8 stpl rnc; do
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

  rm -rf "$d"
}
t_container_roundtrips

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
  local src="$HOME/Downloads/wszst-samples/mp4_mariomdl0.bin"
  [ -f "$src" ] || { sk "Mario Party 4 retail BIN"; return; }
  local d
  d=$(mktemp -d /tmp/_r_mp4_retail.XXXXXX) || { no "Mario Party 4 retail BIN" "mktemp failed"; return; }
  cp "$src" "$d/model.bin"
  ( cd "$d" && timeout 15 "$PWD_PROJECT/wmpbdump" model.bin >run.log 2>&1 )
  local a="$d/model_file0.hsf" b="$d/model_file1.hsf"
  if [ -s "$a" ] && [ -s "$b" ] \
      && [ "$(head -c 7 "$a")" = HSFV037 ] \
      && cmp -s "$a" "$b" \
      && ! grep -q 'Failed\|Unknown Compression' "$d/run.log"; then
    ok "Mario Party 4 retail BIN -> 2 valid HSFV037 models"
  else
    no "Mario Party 4 retail BIN" "$src"
  fi
}
t_mpb_retail

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

echo "== WC24 =="
# --help exits with the usage code by design, so check output not status.
if $B/wwc24crypt --help 2>&1 | grep -q "AES-128-OFB"; then ok "wwc24crypt help"; else no "wwc24crypt help" "unexpected output"; fi
if nm -u "$B/wwc24crypt" 2>/dev/null | grep -qi hmac || nm "$B/wwc24crypt" 2>/dev/null | grep -qi " t .*hmac"; then
  no "wwc24crypt: no HMAC code" "an HMAC symbol is linked in"
else ok "wwc24crypt: no HMAC code linked"; fi

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
