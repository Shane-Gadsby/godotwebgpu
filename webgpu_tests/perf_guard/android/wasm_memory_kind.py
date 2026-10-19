import sys

for path in sys.argv[1:]:
    d = open(path, "rb").read()
    i = 8

    def leb(i):
        r = s = 0
        while True:
            b = d[i]
            i += 1
            r |= (b & 0x7F) << s
            s += 7
            if not b & 0x80:
                return r, i

    out = "no memory import/def found"
    while i < len(d):
        sid = d[i]
        i += 1
        size, i = leb(i)
        end = i + size
        if sid == 2:
            n, j = leb(i)
            for _ in range(n):
                ml, j = leb(j)
                j += ml
                nl, j = leb(j)
                j += nl
                kind = d[j]
                j += 1
                if kind == 2:
                    flags, j = leb(j)
                    _, j = leb(j)
                    if flags & 1:
                        _, j = leb(j)
                    out = f"imports shared={bool(flags & 2)}"
                elif kind == 0:
                    _, j = leb(j)
                elif kind == 1:
                    j += 1
                    fl, j = leb(j)
                    _, j = leb(j)
                    if fl & 1:
                        _, j = leb(j)
                elif kind == 3:
                    j += 2
                elif kind == 4:
                    _, j = leb(j)
        elif sid == 5:
            n, j = leb(i)
            if n:
                flags, j = leb(j)
                out = f"defines own memory shared={bool(flags & 2)}"
        i = end
    print(f"{path.split('/')[-1]:60s} {len(d):>10d}  {out}")
