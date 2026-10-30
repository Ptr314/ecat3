"""List Files-11 ODS-1 volumes stored as physical RX50 dumps (80 tracks x 10 sectors, track 0 first)."""
import sys, os, glob, struct

R50 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789"

def r50(w):
    return R50[w // 1600 % 40] + R50[w // 40 % 40] + R50[w % 40]

def phys_index(lbn):
    t, s = divmod(lbn, 10)
    sec = s * 2 + (1 if s >= 5 else 0)
    sec = (sec + 2 * t) % 10
    track = (t + 1) % 80
    return track * 10 + sec

class Vol:
    def __init__(self, data, logical=False):
        self.d = data
        self.logical = logical
    def blk(self, lbn):
        i = lbn if self.logical else phys_index(lbn)
        return self.d[i * 512:(i + 1) * 512]

def words(b):
    return struct.unpack("<256H", b)

def hdr_ok(b):
    w = words(b)
    return (sum(w[:255]) & 0xFFFF) == w[255]

def retrieval(h):
    mpof = h[1] * 2
    ctsz, lbsz, use = h[mpof + 6], h[mpof + 7], h[mpof + 8]
    out = []
    p = mpof + 10
    for _ in range(use // 2):
        hi, cnt = h[p], h[p + 1]
        lo = struct.unpack("<H", h[p + 2:p + 4])[0]
        out.append(((hi << 16) | lo, cnt + 1))
        p += 4
    return out

def fname(h):
    id_ = h[0] * 2
    w = struct.unpack("<5H", h[id_:id_ + 10])
    return (r50(w[0]) + r50(w[1]) + r50(w[2])).strip() + "." + r50(w[3]).strip() + ";" + oct(w[4])[2:]

def list_vol(path, logical=False):
    v = Vol(open(path, "rb").read(), logical)
    home = v.blk(1)
    ibsz = struct.unpack("<H", home[0:2])[0]
    iblb = (struct.unpack("<H", home[2:4])[0] << 16) | struct.unpack("<H", home[4:6])[0]
    vnam = home[14:26].decode("latin1").rstrip("\0 ")
    if not (0 < ibsz < 50 and 0 < iblb < 800):
        return None, "нет домашнего блока ODS-1"
    hdr0 = iblb + ibsz
    # INDEXF.SYS header gives the whole index file map
    ih = v.blk(hdr0)
    idx_map = []
    for lbn, cnt in retrieval(ih):
        idx_map.extend(range(lbn, lbn + cnt))
    def header(fnum):
        # first 16 headers follow the index bitmap; the rest are found through the INDEXF.SYS map
        if fnum <= 16:
            return v.blk(hdr0 + fnum - 1)
        k = 1 + ibsz + fnum  # 0-based VBN index: VBN 1 boot, 2 home, bitmap, then headers
        if k < len(idx_map):
            return v.blk(idx_map[k])
        return None
    files = []; bad = []; total = 0
    def read_file(h):
        data = b""
        for lbn, cnt in retrieval(h):
            for i in range(cnt):
                data += v.blk(lbn + i)
        return data
    def walk_dir(h, prefix, depth=0):
        nonlocal total
        data = read_file(h)
        for i in range(0, len(data), 16):
            e = data[i:i + 16]
            fnum = struct.unpack("<H", e[0:2])[0]
            if fnum == 0 or fnum == 0x6DB6: continue
            fh = header(fnum)
            name = (r50(struct.unpack("<H", e[6:8])[0]) + r50(struct.unpack("<H", e[8:10])[0]) + r50(struct.unpack("<H", e[10:12])[0])).strip() + "." + r50(struct.unpack("<H", e[12:14])[0]).strip()
            if fh is None: bad.append(prefix + name + "(нет заголовка)"); continue
            ok = hdr_ok(fh)
            size = sum(c for _, c in retrieval(fh)) if ok else 0
            total += size
            files.append((prefix + name, size, ok))
            if not ok: bad.append(prefix + name)
            if name.endswith(".DIR") and ok and depth < 2 and not name.startswith("000000"):
                walk_dir(fh, prefix + "[" + name[:-4] + "]", depth + 1)
    mfd = header(4)
    if mfd is None or not hdr_ok(mfd):
        return (vnam, [], ["MFD повреждён"], 0), None
    walk_dir(mfd, "")
    return (vnam, files, bad, total), None

if __name__ == "__main__":
    for path in sys.argv[1:]:
        res, err = list_vol(path)
        if err:
            print(f"== {path}: {err}"); continue
        vnam, files, bad, total = res
        names = [f for f, s, ok in files if not f.endswith(".DIR")]
        print(f"== {os.path.basename(os.path.dirname(path))}/{os.path.basename(path)}  том '{vnam}'  файлов {len(names)}  блоков {total}  битых заголовков {len(bad)}")
        print("   ", " ".join(n for n in names[:60]))
        if bad: print("    битые:", bad[:15])
