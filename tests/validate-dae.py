#!/usr/bin/env python3
"""Validate the COLLADA invariants that XML parsing/import smoke tests miss."""

import argparse
import math
import os
import sys
import xml.etree.ElementTree as ET


NS = "{http://www.collada.org/2005/11/COLLADASchema}"


def validate(path, require_images=False):
    errors = []
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        return [f"XML: {exc}"]

    source_counts = {}
    for source in root.iter(NS + "source"):
        accessor = source.find(".//" + NS + "accessor")
        if accessor is not None:
            source_counts[source.get("id")] = int(accessor.get("count", "0"))
        array = source.find(NS + "float_array")
        if array is not None:
            try:
                values = [float(value) for value in (array.text or "").split()]
            except ValueError as exc:
                errors.append(f"{array.get('id')}: invalid float: {exc}")
                continue
            if any(not math.isfinite(value) for value in values):
                errors.append(f"{array.get('id')}: non-finite value")
            declared = int(array.get("count", "0"))
            if declared != len(values):
                errors.append(f"{array.get('id')}: {len(values)} values, declares {declared}")

    vertices = {}
    for node in root.iter(NS + "vertices"):
        position = next((item for item in node.findall(NS + "input")
                         if item.get("semantic") == "POSITION"), None)
        if position is not None:
            vertices[node.get("id")] = position.get("source", "").lstrip("#")

    triangles_seen = 0
    for triangles in root.iter(NS + "triangles"):
        inputs = triangles.findall(NS + "input")
        stride = max((int(item.get("offset", "0")) for item in inputs), default=-1) + 1
        try:
            values = [int(value) for p in triangles.findall(NS + "p")
                      for value in (p.text or "").split()]
        except ValueError as exc:
            errors.append(f"triangles: invalid index: {exc}")
            continue
        count = int(triangles.get("count", "0"))
        triangles_seen += count
        expected = count * 3 * stride
        if len(values) != expected:
            errors.append(f"triangles: {len(values)} tuple values, expected {expected}")
            continue
        for item in inputs:
            offset = int(item.get("offset", "0"))
            source = item.get("source", "").lstrip("#")
            if item.get("semantic") == "VERTEX":
                source = vertices.get(source, source)
            limit = source_counts.get(source)
            if limit is None:
                errors.append(f"triangles: missing source {source}")
                continue
            bad = next((value for value in values[offset::stride]
                        if value < 0 or value >= limit), None)
            if bad is not None:
                errors.append(f"triangles: {item.get('semantic')} index {bad} outside 0..{limit-1}")

    if triangles_seen == 0:
        errors.append("no triangles")

    if require_images:
        for image in root.iter(NS + "image"):
            init = image.find(NS + "init_from")
            if init is None or not (init.text or "").strip():
                errors.append(f"image {image.get('id')}: no init_from")
                continue
            target = os.path.normpath(os.path.join(os.path.dirname(path), init.text.strip()))
            if not os.path.isfile(target):
                errors.append(f"image {image.get('id')}: missing {init.text.strip()}")
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
