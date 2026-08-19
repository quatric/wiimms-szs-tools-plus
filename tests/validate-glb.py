#!/usr/bin/env python3
"""Validate/parse GLB (binary glTF) files.

Mirrors validate-dae.py's role now that the default model-export format is
GLB, not DAE (see commit ac04d55). Usable two ways:

  1. As a CLI smoke test, same contract as validate-dae.py:
         validate-glb.py [--require-images] file.glb [file.glb ...]
     exits nonzero and prints one "path: error" line per problem found.

  2. As a library, imported by regress.sh's inline `python3 - <<'PY'` blocks
     that need to inspect nodes/skins/accessors directly (bind-pose matrices,
     joint hierarchies, face winding, etc.) -- the same role validate-dae.py's
     ElementTree parsing played for the old DAE-based assertions.
     `load_glb(path)` returns the parsed JSON dict plus a `read_accessor(i)`
     closure bound to that file's binary chunk.
"""

import argparse
import json
import math
import os
import struct
import sys


COMPONENT_TYPES = {
    5120: ("b", 1),   # BYTE
    5121: ("B", 1),   # UNSIGNED_BYTE
    5122: ("h", 2),   # SHORT
    5123: ("H", 2),   # UNSIGNED_SHORT
    5125: ("I", 4),   # UNSIGNED_INT
    5126: ("f", 4),   # FLOAT
}

TYPE_COMPONENTS = {
    "SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
    "MAT2": 4, "MAT3": 9, "MAT4": 16,
}


class GLB:
    def __init__(self, path, json_data, bin_chunk):
        self.path = path
        self.json = json_data
        self.bin = bin_chunk

    def buffer_view_bytes(self, bv_index):
        bv = self.json["bufferViews"][bv_index]
        offset = bv.get("byteOffset", 0)
        length = bv["byteLength"]
        return self.bin[offset:offset + length]

    def read_accessor(self, acc_index):
        """Return a flat list of numbers (or list-of-tuples for vec/mat types
        collapsed to flat scalars) for accessor `acc_index`, honoring
        byteStride if the underlying bufferView is interleaved."""
        acc = self.json["accessors"][acc_index]
        fmt_char, comp_size = COMPONENT_TYPES[acc["componentType"]]
        n_comp = TYPE_COMPONENTS[acc["type"]]
        count = acc["count"]
        bv_index = acc.get("bufferView")
        if bv_index is None:
            return [0] * (count * n_comp)
        bv = self.json["bufferViews"][bv_index]
        base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = bv.get("byteStride") or comp_size * n_comp
        out = []
        for i in range(count):
            rec_off = base + i * stride
            for c in range(n_comp):
                off = rec_off + c * comp_size
                (val,) = struct.unpack_from("<" + fmt_char, self.bin, off)
                out.append(val)
        return out

    def read_accessor_tuples(self, acc_index):
        acc = self.json["accessors"][acc_index]
        n_comp = TYPE_COMPONENTS[acc["type"]]
        flat = self.read_accessor(acc_index)
        return [tuple(flat[i:i + n_comp]) for i in range(0, len(flat), n_comp)]

    def image_bytes(self, image_index):
        img = self.json["images"][image_index]
        if "bufferView" in img:
            return self.buffer_view_bytes(img["bufferView"])
        return None


def load_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[0:4] != b"glTF":
        raise ValueError("not a GLB file (bad magic)")
    version, total_length = struct.unpack_from("<II", data, 4)
    json_data = None
    bin_chunk = b""
    off = 12
    while off < total_length and off < len(data):
        chunk_length, chunk_type = struct.unpack_from("<I4s", data, off)
        chunk_data = data[off + 8: off + 8 + chunk_length]
        if chunk_type == b"JSON":
            json_data = json.loads(chunk_data.decode("utf-8"))
        elif chunk_type == b"BIN\x00":
            bin_chunk = chunk_data
        off += 8 + chunk_length
    if json_data is None:
        raise ValueError("no JSON chunk found")
    return GLB(path, json_data, bin_chunk)


def mat4_mul(a, b):
    """Column-major 4x4 matrix multiply (glTF convention), a*b."""
    r = [0.0] * 16
    for col in range(4):
        for row in range(4):
            s = 0.0
            for k in range(4):
                s += a[k * 4 + row] * b[col * 4 + k]
            r[col * 4 + row] = s
    return r


def mat4_identity():
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0]


def quat_to_mat4(q):
    x, y, z, w = q
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0.0,
        2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0.0,
        2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def node_local_matrix(node):
    if "matrix" in node:
        return list(node["matrix"])
    t = node.get("translation", [0.0, 0.0, 0.0])
    r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = node.get("scale", [1.0, 1.0, 1.0])
    m = quat_to_mat4(r)
    # apply scale to rotation columns
    for col in range(3):
        for row in range(4):
            m[col * 4 + row] *= s[col]
    m[12], m[13], m[14] = t
    return m


def node_world_matrix(glb, node_index, cache=None):
    """World matrix of node `node_index`, found by locating its parent chain
    via a one-time parent map (glTF nodes only point child->parent implicitly
    through children[] lists, so this walks all nodes once)."""
    if cache is None:
        cache = {}
    if node_index in cache:
        return cache[node_index]
    nodes = glb.json["nodes"]
    parent = None
    for i, n in enumerate(nodes):
        if node_index in n.get("children", []):
            parent = i
            break
    local = node_local_matrix(nodes[node_index])
    if parent is None:
        world = local
    else:
        world = mat4_mul(node_world_matrix(glb, parent, cache), local)
    cache[node_index] = world
    return world


def validate(path, require_images=False):
    errors = []
    try:
        glb = load_glb(path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return [f"GLB: {exc}"]

    j = glb.json
    meshes = j.get("meshes", [])
    if not meshes:
        errors.append("no meshes")

    accessors = j.get("accessors", [])
    for mesh in meshes:
        for prim in mesh.get("primitives", []):
            attrs = prim.get("attributes", {})
            if "POSITION" not in attrs:
                errors.append(f"mesh {mesh.get('name')}: primitive missing POSITION")
                continue
            pos_count = accessors[attrs["POSITION"]]["count"]
            for sem, acc_idx in attrs.items():
                acc = accessors[acc_idx]
                if sem in ("POSITION", "NORMAL") and acc["count"] != pos_count:
                    errors.append(f"mesh {mesh.get('name')}: {sem} count {acc['count']} != POSITION count {pos_count}")
            if "indices" in prim:
                idx = glb.read_accessor(prim["indices"])
                if len(idx) == 0:
                    errors.append(f"mesh {mesh.get('name')}: empty indices")
                elif len(idx) % 3 != 0:
                    errors.append(f"mesh {mesh.get('name')}: index count {len(idx)} not a multiple of 3")
                bad = next((v for v in idx if v < 0 or v >= pos_count), None)
                if bad is not None:
                    errors.append(f"mesh {mesh.get('name')}: index {bad} outside 0..{pos_count-1}")
            positions = glb.read_accessor_tuples(attrs["POSITION"])
            if any(not all(math.isfinite(c) for c in p) for p in positions):
                errors.append(f"mesh {mesh.get('name')}: non-finite position")

    if require_images:
        images = j.get("images", [])
        if not images:
            errors.append("no images")
        for i, img in enumerate(images):
            blob = glb.image_bytes(i)
            if not blob:
                errors.append(f"image {i} ({img.get('name')}): no embedded data")
                continue
            if not (blob[:8] == b"\x89PNG\r\n\x1a\n" or blob[:2] == b"\xff\xd8"):
                errors.append(f"image {i} ({img.get('name')}): not a recognizable PNG/JPEG")

    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--require-images", action="store_true")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    failed = False
    for path in args.files:
        for error in validate(path, args.require_images):
            failed = True
            print(f"{path}: {error}", file=sys.stderr)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
