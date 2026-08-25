import re, sys

def dae_data(path):
    txt = open(path).read()
    P = U = None
    for mm in re.finditer(r'<source[\s\S]*?</source>', txt):
        s = mm.group(0)
        fa = re.search(r'<float_array[^>]*>([^<]+)</float_array>', s)
        ta = re.search(r'stride="(\d+)"', s)
        if not fa or not ta:
            continue
        vals = [float(x) for x in fa.group(1).split()]
        stride = int(ta.group(1))
        if stride == 3 and P is None:
            P = vals
        elif stride == 2 and U is None:
            U = vals
    assert P is not None, "no positions"
    prim = re.search(r'<triangles[\s\S]*?</triangles>|<polylist[\s\S]*?</polylist>', txt)
    assert prim, "no primitives"
    body = prim.group(0)
    pin = re.search(r'semantic="VERTEX"[^>]*offset="(\d+)"', body)
    tin = re.search(r'semantic="TEXCOORD"[^>]*offset="(\d+)"', body)
    poff = int(pin.group(1)) if pin else 0
    toff = int(tin.group(1)) if tin else -1
    ncol = max(poff, toff) + 1
    idxs = [int(x) for x in re.search(r'<p>([\s\d]+)</p>', body).group(1).split()]
    tris = []
    for base in range(0, len(idxs), ncol * 3):
        tri = []
        for k in range(3):
            i = idxs[base + k * ncol : base + k * ncol + ncol]
            j = i[poff] * 3
            pos = (P[j], P[j+1], P[j+2])
            uv = (0.0, 0.0)
            if toff >= 0 and U:
                j = i[toff] * 2
                uv = (U[j], 1.0 - U[j+1])
            tri.append(pos + uv)
        tris.append(tuple(sorted(tri)))
    return tris

TOL = 2e-4  # covers s16/shift-13 requantization (6.1e-5) + %f text rounding

def close(a, b):
    return all(abs(x - y) <= TOL for ca, cb in zip(a, b) for x, y in zip(ca, cb))

A = dae_data(sys.argv[1])
B = dae_data(sys.argv[2])
if len(A) != len(B):
    print(f"MISMATCH: triangle count {len(A)} != {len(B)}")
    sys.exit(1)
unused = list(B)
unmatched = []
for t in A:
    for k, u in enumerate(unused):
        if close(t, u):
            unused.pop(k)
            break
    else:
        unmatched.append(t)
if unmatched or unused:
    print(f"MISMATCH: {len(unmatched)} unmatched, {len(unused)} extra")
    for t in unmatched[:2]: print("  A", t)
    for t in unused[:2]: print("  B", t)
    sys.exit(1)
print(f"MATCH ({len(A)} triangles)")
