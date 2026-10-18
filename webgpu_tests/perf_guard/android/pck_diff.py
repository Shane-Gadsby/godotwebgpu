# List a Godot 4 .pck (format v2/v3): path, size. Used to see what differs between two exports.
import struct
import sys


def lst(p):
    d = open(p, "rb").read()
    assert d[:4] == b"GDPC", "not a pck"
    (ver,) = struct.unpack_from("<I", d, 4)
    off = 8 + 12  # version + engine version
    flags, files_base = struct.unpack_from("<IQ", d, off)
    off += 12
    if ver >= 3:
        (dir_off,) = struct.unpack_from("<Q", d, off)
        off += 8
    off += 16 * 4  # reserved
    if ver >= 3:
        off = dir_off
    (n,) = struct.unpack_from("<I", d, off)
    off += 4
    out = {}
    for _ in range(n):
        (pl,) = struct.unpack_from("<I", d, off)
        off += 4
        path = d[off : off + pl].rstrip(b"\0").decode(errors="replace")
        off += pl
        o, sz = struct.unpack_from("<QQ", d, off)
        off += 16
        off += 16 + 4  # md5 + flags
        out[path] = sz
    return out


a, b = lst(sys.argv[1]), lst(sys.argv[2])
print(
    f"{sys.argv[1].split('/')[-3]}: {len(a)} files {sum(a.values()) / 1e6:.1f} MB | {sys.argv[2].split('/')[-3]}: {len(b)} files {sum(b.values()) / 1e6:.1f} MB"
)
print("--- only in first (top by size):")
for k in sorted(set(a) - set(b), key=lambda k: -a[k])[:10]:
    print(f"  {a[k]:>10d} {k}")
print("--- only in second:")
for k in sorted(set(b) - set(a), key=lambda k: -b[k])[:10]:
    print(f"  {b[k]:>10d} {k}")
print("--- in both, differing size:")
for k in sorted(set(a) & set(b), key=lambda k: -abs(a[k] - b[k]))[:6]:
    if a[k] != b[k]:
        print(f"  {a[k]:>10d} vs {b[k]:>10d} {k}")
print("--- textures in each:")
for name, m in (("first", a), ("second", b)):
    print(" ", name, [f"{k.split('/')[-1][-40:]}={v}" for k, v in m.items() if k.endswith(".ctex")])
