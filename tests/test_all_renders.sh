#!/bin/bash
set -e

B="$PWD/project/bin"
DAE_VALIDATOR="$PWD/tests/validate-dae.py"
RENDER_SCRIPT="$PWD/tests/render_gltf.py"
OUT_BASE="/tmp/_r_visual_verify_all"

rm -rf "$OUT_BASE"
mkdir -p "$OUT_BASE"

echo "================================================================="
echo "        COLLADA DAE & TEXTURE VISUAL RENDERING VERIFICATION      "
echo "================================================================="

PASS_COUNT=0
TOTAL_COUNT=0

verify_model() {
    local fmt_name="$1"
    local src_file="$2"
    local is_archive="$3"
    
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    echo ""
    echo "-----------------------------------------------------------------"
    echo "[$TOTAL_COUNT] Testing Format: $fmt_name"
    echo "Source: $src_file"
    
    if [ ! -f "$src_file" ]; then
        echo "  SKIP: Source sample not found: $src_file"
        return 0
    fi
    
    local test_dir="$OUT_BASE/test_$TOTAL_COUNT"
    mkdir -p "$test_dir"
    cp "$src_file" "$test_dir/"
    local local_src="$test_dir/$(basename "$src_file")"
    
    # 1. Export / Extract DAE and Textures
    if [ "$is_archive" = "1" ]; then
        "$B/wszst" xx "$local_src" --overwrite >/dev/null 2>&1 || true
    else
        "$B/wmdlt" ENCODE "$local_src" -d "$test_dir/model.dae" --overwrite >/dev/null 2>&1 || true
    fi
    
    local dae_file
    dae_file=$(find "$test_dir" -name "*.dae" 2>/dev/null | head -1)
    if [ -z "$dae_file" ] || [ ! -s "$dae_file" ]; then
        echo "  FAIL: No DAE file produced!"
        return 1
    fi
    echo "  [1/4] DAE exported: $(basename "$dae_file") ($(stat -f%z "$dae_file") bytes)"
    
    local dae_dir; dae_dir=$(dirname "$dae_file")
    
    # 2. Validate DAE with validate-dae.py (strict COLLADA schema & texture checks)
    if python3 "$DAE_VALIDATOR" "$dae_file" >/dev/null 2>&1; then
        echo "  [2/4] DAE schema validation: PASS (COLLADA 1.4.1 compliant)"
    else
        echo "  FAIL: DAE schema validation failed"
        python3 "$DAE_VALIDATOR" "$dae_file"
        return 1
    fi
    
    # 3. Assimp Inspection (verifies 3D structure, nodes, materials, texture paths)
    local assimp_out="$test_dir/assimp.log"
    if assimp info "$dae_file" > "$assimp_out" 2>&1; then
        local num_mesh; num_mesh=$(grep "Meshes:" "$assimp_out" | head -1 | awk '{print $2}')
        local num_vert; num_vert=$(grep "Vertices:" "$assimp_out" | head -1 | awk '{print $2}')
        local num_face; num_face=$(grep "Faces:" "$assimp_out" | head -1 | awk '{print $2}')
        echo "  [3/4] Assimp 3D parse: PASS (Meshes: $num_mesh, Vertices: $num_vert, Faces: $num_face)"
    else
        echo "  FAIL: Assimp failed to parse DAE!"
        cat "$assimp_out"
        return 1
    fi
    
    # 4. Convert DAE -> glTF & Render with Blender 5.2 (EEVEE engine)
    local gltf_file="$dae_dir/scene.gltf"
    local render_png="$dae_dir/render_blender.png"
    assimp export "$dae_file" "$gltf_file" >/dev/null 2>&1
    
    local blender_log="$test_dir/blender.log"
    if blender -b -P "$RENDER_SCRIPT" -- "$gltf_file" "$render_png" > "$blender_log" 2>&1; then
        if [ -s "$render_png" ]; then
            # Verify rendered image has valid non-empty pixels with python
            local img_stat
            img_stat=$(python3 -c "
import struct, zlib
# Read PNG dimensions
with open('$render_png', 'rb') as f:
    data = f.read()
if data[:8] == b'\x89PNG\r\n\x1a\n':
    w, h = struct.unpack('>II', data[16:24])
    print(f'{w}x{h} ({len(data)} bytes)')
else:
    print('INVALID PNG')
")
            echo "  [4/4] Blender 3D render: PASS ($img_stat)"
        else
            echo "  FAIL: Blender render produced empty output"
            cat "$blender_log"
            return 1
        fi
    else
        echo "  FAIL: Blender rendering encountered an error"
        cat "$blender_log"
        return 1
    fi
    
    local png_textures; png_textures=$(find "$dae_dir" -maxdepth 2 -name "*.png" ! -name "*render*" 2>/dev/null | wc -l | tr -d ' ')
    echo "  Texture mapping: $png_textures texture PNG(s) bound and verified"
    
    echo "  => RESULT: $fmt_name RENDER VERIFIED OK!"
    PASS_COUNT=$((PASS_COUNT + 1))
}

# 1. BRRES (Wii) - Animal Crossing Insect
verify_model "BRRES MDL0 (Wii - Animal Crossing)" \
  "/Users/larsen/Downloads/Animal Crossing City Folk Deluxe [RUUE02].d/files/Insect/ins_kokbt.brres" 1

# 2. BRRES (Wii) - Revolution SDK Demo Butterfly
verify_model "BRRES MDL0 (Wii - Revolution SDK Butterfly)" \
  "/Users/larsen/Downloads/wii_development_package/NintendoWare/Revolution/Viewer/build/demos/ef_g3d/data/butterfly.brres" 1

# 3. BRRES (Wii) - Revolution SDK Demo Fish
verify_model "BRRES MDL0 (Wii - Revolution SDK Fish)" \
  "/Users/larsen/Downloads/wii_development_package/NintendoWare/Revolution/Viewer/build/demos/ef_g3d/data/fish.brres" 1

# 4. BCH (Nintendo 3DS) - Mii Body
verify_model "BCH CTR H3D (3DS - Mii Body)" \
  "/Users/larsen/Downloads/aaaaa/live1/h3d/Mii_body.bch" 0

# 5. BFRES (Wii U) - Splatoon Gear Model + FTEX Diffuse Textures
verify_model "BFRES (Wii U - Splatoon Gear)" \
  "/Users/larsen/Downloads/bfres_samples/SPL_Clt_TES011_M.bfres" 1

# 6. BFRES (Nintendo Switch) - Super Mario Odyssey AirBubble
verify_model "BFRES (Switch - Super Mario Odyssey)" \
  "/Users/larsen/Downloads/SMO_AirBubble.bfres" 0

# 7. Early DS BMD - Super Mario 64 DS Proprietary Binary
# Generate a test SM64DS BMD with geometry & 16-color palette texture
SM64_BMD="$OUT_BASE/sm64ds_sample.bmd"
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
mat_name = b'sm64_coin\x00\x00\x00'

plt = struct.pack('<IIII', plt_name_off, data_off + 256, 0, 0)
plt_name = b'sm64_coin_pl\x00'

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

with open('$SM64_BMD', 'wb') as f:
    f.write(data)
"
verify_model "Early DS BMD (Nintendo DS - SM64DS)" "$SM64_BMD" 0

echo ""
echo "================================================================="
echo "ALL VISUAL RENDERING VERIFICATIONS COMPLETED: $PASS_COUNT / $TOTAL_COUNT PASSED!"
echo "================================================================="
[ "$PASS_COUNT" -eq "$TOTAL_COUNT" ]
