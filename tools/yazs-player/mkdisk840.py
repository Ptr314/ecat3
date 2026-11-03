# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Диск ДОС 3.3 Агата на 840 КБ (160 логических дорожек по 21 сектору): чтение
каталога, удаление и запись файлов. Раскладка - как в dsk_tools
(fs_dos33.*, image_agat840.cpp): сектор (T*21 + S)*256, VTOC в T17 S0,
свободные сектора - по 4 байта на дорожку (маски VTOCMask840) в VTOC+$38 для
дорожек до $32 и в секторах $32/0 и $72/0 для остальных.
"""

import struct

import tables

SECTORS = 21
TRACKS = 160
SIZE = SECTORS * TRACKS * 256
MASK = [0x00080000, 0x00100000, 0x00200000, 0x00400000, 0x00800000, 0x00000100, 0x00000200,
        0x00000400, 0x00000800, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00000001,
        0x00000002, 0x00000004, 0x00000008, 0x00000010, 0x00000020, 0x00000040, 0x00000080]
T_TEXT, T_BIN = 0x00, 0x04
LOCKED = 0x80
ENTRY = 35
PAIRS = 122


class Disk840(object):
    def __init__(self, data):
        if len(data) < SIZE:
            raise ValueError("образ меньше 840 КБ")
        self.d = bytearray(data[:SIZE])

    def off(self, t, s):
        return (t * SECTORS + s) * 256

    def sector(self, t, s):
        o = self.off(t, s)
        return self.d[o:o + 256]

    def put(self, t, s, data):
        assert len(data) <= 256
        o = self.off(t, s)
        self.d[o:o + 256] = bytes(data) + bytes(256 - len(data))

    # --- свободные сектора ---

    def _map(self, t):
        if t < 0x32:
            return self.off(17, 0) + 0x38 + 4 * t
        if t < 0x72:
            return self.off(0x32, 0) + 4 * (t - 0x32)
        return self.off(0x72, 0) + 4 * (t - 0x72)

    def is_free(self, t, s):
        return bool(struct.unpack_from("<I", self.d, self._map(t))[0] & MASK[s])

    def set_free(self, t, s, free):
        o = self._map(t)
        v = struct.unpack_from("<I", self.d, o)[0]
        v = (v | MASK[s]) if free else (v & ~MASK[s])
        struct.pack_into("<I", self.d, o, v)

    def alloc(self):
        # Ближе к каталогу - быстрее грузится
        for t in list(range(18, TRACKS)) + list(range(16, 0, -1)):
            if t in (0x32, 0x72):
                continue
            for s in range(SECTORS):
                if self.is_free(t, s):
                    self.set_free(t, s, False)
                    return t, s
        raise ValueError("диск полон")

    # --- каталог ---

    def catalog_sectors(self):
        v = self.sector(17, 0)
        t, s = v[1], v[2]
        out = []
        while t:
            out.append((t, s))
            c = self.sector(t, s)
            t, s = c[1], c[2]
        return out

    def entries(self):
        """(дорожка каталога, сектор, смещение, запись) для занятых записей"""
        out = []
        for t, s in self.catalog_sectors():
            c = self.sector(t, s)
            for i in range(7):
                o = 0x0B + ENTRY * i
                if c[o] == 0:
                    return out
                if c[o] == 0xFF:
                    continue
                out.append((t, s, o, bytes(c[o:o + ENTRY])))
        return out

    def file_sectors(self, ts_t, ts_s):
        """Сектора файла: (списки T/S, данные)"""
        lists, data = [], []
        while ts_t:
            lists.append((ts_t, ts_s))
            ts = self.sector(ts_t, ts_s)
            for i in range(PAIRS):
                t, s = ts[0x0C + 2 * i], ts[0x0D + 2 * i]
                if t == 0 and s == 0:
                    continue
                data.append((t, s))
            ts_t, ts_s = ts[1], ts[2]
        return lists, data

    def listing(self):
        out = []
        for t, s, o, e in self.entries():
            name = decode_name(e[3:33])
            out.append((e[2], name, struct.unpack_from("<H", e, 33)[0]))
        return out

    def read_file(self, name):
        for t, s, o, e in self.entries():
            if decode_name(e[3:33]) == name:
                _, data = self.file_sectors(e[0], e[1])
                return b"".join(bytes(self.sector(a, b)) for a, b in data)
        raise KeyError(name)

    def delete(self, name):
        for t, s, o, e in self.entries():
            if decode_name(e[3:33]) == name:
                lists, data = self.file_sectors(e[0], e[1])
                for a, b in lists + data:
                    self.set_free(a, b, True)
                c = self.sector(t, s)
                c[o + 32] = c[o]          # как ДОС: дорожка - в последний знак имени
                c[o] = 0xFF
                self.put(t, s, c)
                return
        raise KeyError(name)

    def add(self, name, ftype, body, locked=False):
        """body - тело файла как оно лежит в секторах (у B - с адресом и длиной)"""
        for _, n, _ in self.listing():
            if n == name:
                raise ValueError("файл %s уже есть" % name)
        nsec = (len(body) + 255) // 256
        data = []
        for i in range(nsec):
            t, s = self.alloc()
            self.put(t, s, body[i * 256:(i + 1) * 256])
            data.append((t, s))
        lists = []
        for k in range(0, max(1, nsec), PAIRS):
            lists.append(self.alloc())
        for k, (lt, ls) in enumerate(lists):
            ts = bytearray(256)
            if k + 1 < len(lists):
                ts[1], ts[2] = lists[k + 1]
            struct.pack_into("<H", ts, 5, k * PAIRS)
            for i, (t, s) in enumerate(data[k * PAIRS:(k + 1) * PAIRS]):
                ts[0x0C + 2 * i], ts[0x0D + 2 * i] = t, s
            self.put(lt, ls, ts)
        # свободная запись каталога
        for t, s in self.catalog_sectors():
            c = self.sector(t, s)
            for i in range(7):
                o = 0x0B + ENTRY * i
                if c[o] in (0, 0xFF):
                    e = bytearray(ENTRY)
                    e[0], e[1] = lists[0]
                    e[2] = ftype | (LOCKED if locked else 0)
                    e[3:33] = tables.name_dos(name)
                    struct.pack_into("<H", e, 33, nsec + len(lists))
                    c[o:o + ENTRY] = e
                    self.put(t, s, c)
                    return
        raise ValueError("каталог полон")

    def set_volume(self, name):
        o = self.off(17, 0)
        self.d[o + 8:o + 8 + 31] = tables.agat(name, 31, upper=True)


def decode_name(raw):
    rev = {v: k for k, v in tables.AGAT.items() if k not in "Ёё"}
    s = "".join(rev.get(b, "?") for b in raw)
    return s.rstrip()


def b_body(addr, data):
    return struct.pack("<HH", addr, len(data)) + bytes(data)


if __name__ == "__main__":
    import sys
    d = Disk840(open(sys.argv[1], "rb").read())
    for ftype, name, n in d.listing():
        print("%02X %4d  %s" % (ftype, n, name))
