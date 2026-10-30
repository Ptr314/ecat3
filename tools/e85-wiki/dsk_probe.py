import sys, os, glob, struct, collections
D = sys.argv[1]
def sigs(b):
    out = []
    for i in range(0, len(b), 512):
        s = b[i:i+512]
        for tag in (b"DECFILE11A", b"DECRT11A"):
            k = s.find(tag)
            if k >= 0:
                vol = s[0o730:0o730+12] if tag == b"DECFILE11A" else s[0o730:0o730+12]
                out.append((i // 512, tag.decode(), k, vol.decode("latin1").strip()))
    return out
for f in sorted(glob.glob(os.path.join(D, "**", "*.dsk"), recursive=True)):
    b = open(f, "rb").read()
    rel = os.path.relpath(f, D)
    zero = sum(1 for i in range(0, len(b), 512) if b[i:i+512].count(0) == 512 or b[i:i+512].count(0xE5) == 512)
    fat = ""
    if b[510:512] == b"\x55\xaa" or b[3:11].isascii() and b[3:11].strip(b" \x00").isalnum():
        fat = "FAT oem=%r bps=%d spc=%d sec=%d" % (b[3:11], struct.unpack("<H", b[11:13])[0], b[13], struct.unpack("<H", b[19:21])[0])
    s = sigs(b)
    boot0 = struct.unpack("<H", b[0:2])[0]
    print(f"{rel:32s} empty={zero:3d} boot0={boot0:06o} {fat} {s[:3]}")
