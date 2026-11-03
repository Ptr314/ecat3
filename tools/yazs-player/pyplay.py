# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Эталонная модель движка .YZ: тик за тиком то же, что делает player.a65.
Дает записи в регистры ЯЗС и состояние после каждого тика - по ним
проверяется проигрыватель на 6502 (сценарий снимает yazs.regs/counters и
счетчик тиков из ОЗУ и сравнивает с моделью).

    python pyplay.py файл.yz [тиков]
"""

import sys

import tables

PTAB = tables.period_table()
VIB = tables.vibrato_table()
CNT_ADDR = [0, 1, 2, 4, 5]          # регистры счетчиков тонов 1-5


def s8(v):
    return v - 256 if v & 0x80 else v


def w(d, o):
    return d[o] | (d[o + 1] << 8)


def period_of(total):
    q, f = total >> 4, total & 15
    octv, pc = q // 12, q % 12
    p = PTAB[pc * 16 + f]
    if octv:
        p = (p + (1 << (octv - 1))) >> octv
    return p


class Tone(object):
    def __init__(self):
        self.ins = 0
        self.pend = None            # None, "note", "legato"
        self.pend_note = 0
        self.pend_ins = None
        self.delay = 0
        self.cut = 0                # 0 - нет; иначе тиков до отпускания + 1
        self.cut_req = 0
        self.state = 0
        self.regpos = self.arppos = self.pitpos = 0
        self.cur = self.target = 0
        self.porta = 0
        self.slide = 0
        self.arp_fx = 0
        self.arp_phase = 0
        self.vib_fx = 0
        self.vib_cnt = 0
        self.vib_phase = 0
        self.det_fx = 0
        self.bus_fx = 0xFF
        self.trem = 0
        self.trem_phase = 0
        self.transpose = 0
        self.retrig = 0
        self.retrig_cnt = 0
        self.last_reg = 0
        self.last_per = -1


class Player(object):
    def __init__(self, data):
        self.d = data
        assert data[:2] == b"YZ"
        self.n = data[66]
        self.speed = data[67]
        self.frames = data[68]
        self.loop = data[69] if data[3] & 1 else None
        self.o_ord = w(data, 70)
        self.o_pat = w(data, 72)
        self.o_ins = w(data, 74)
        self.o_txt = w(data, 76)
        self.writes = []            # (тик, адрес регистра, значение)
        self.ticks = 0
        self.regs = [0] * 16
        self.tone = [Tone() for _ in range(5)]
        self.drum_pend = [None, None]
        self.drum_preset = [0, 0]
        self.text = 0xFF
        self.ended = False
        self.tick = 0
        self.row = 0
        self.jump = None
        self.brk = False
        self.write(7, 0xB6)
        self.write(6, self.n)
        self.write(6, 0)
        self.load_frame(0)

    def write(self, a, v):
        self.writes.append((self.ticks, a, v))
        self.regs[a] = v

    def load_frame(self, f):
        self.frame = f
        o = self.o_ord + 9 * f
        self.frame_rows = self.d[o]
        self.ptr = [w(self.d, self.o_pat + 2 * self.d[o + 1 + ch]) for ch in range(8)]
        self.wait = [0] * 8
        self.row = 0

    def ins_rec(self, i):
        return w(self.d, self.o_ins + 2 * i)

    # --- разбор строки ---

    def byte(self, ch):
        v = self.d[self.ptr[ch]]
        self.ptr[ch] += 1
        return v

    def parse(self, ch):
        if self.wait[ch]:
            self.wait[ch] -= 1
            return
        t = self.tone[ch] if ch < 5 else None
        if t:
            t.delay = 0
            t.cut_req = 0
        while True:
            b = self.byte(ch)
            if b >= 0x80:
                self.wait[ch] = b - 0x80
                return
            if b < 0x60:
                if t:
                    t.pend, t.pend_note = "note", b
                elif ch < 7:
                    self.drum_pend[ch - 5] = b
            elif b in (0x60, 0x61):
                if t:
                    t.pend = None
                    if b == 0x60:
                        if t.state == 1:
                            t.state, t.regpos = 2, 0
                    else:
                        t.state = 0
                        self.reg_out(ch, t, 0)
                elif ch < 7:
                    self.drum_pend[ch - 5] = None
                    v = self.drum_preset[ch - 5]
                    self.write(13 + ch - 5, v)
            elif b == 0x62:
                i = self.byte(ch)
                if t:
                    t.pend_ins = i
            elif b == 0x63:
                n = self.byte(ch)
                if t:
                    t.pend, t.pend_note = "legato", n
                elif ch < 7:
                    self.drum_pend[ch - 5] = n
            else:
                self.effect(ch, t, b - 0x64, self.byte(ch))

    def effect(self, ch, t, e, p):
        if e == 10:
            self.speed = p
        elif e == 11:
            self.n = p
            self.write(6, p)
            self.write(6, 0)
        elif e == 12:
            self.jump = p
        elif e == 13:
            self.text = p
        elif e == 16:
            self.brk = True
        elif t is None:
            return
        elif e == 0:
            t.arp_fx = p
        elif e == 1:
            t.slide = p
        elif e == 2:
            t.slide = -p
        elif e == 3:
            t.porta = p
        elif e == 4:
            t.vib_fx = p
        elif e == 5:
            t.det_fx = s8(p)
        elif e == 6:
            t.bus_fx = p
        elif e == 7:
            t.trem = p
            t.trem_phase = 0
        elif e == 8:
            t.delay = p
        elif e == 9:
            t.cut_req = p
        elif e == 14:
            t.transpose = s8(p)
        elif e == 15:
            t.retrig = p
            t.retrig_cnt = 0

    # --- тик ---

    def reg_out(self, ch, t, v):
        if v != t.last_reg:
            t.last_reg = v
            self.write(8 + ch, v)

    def macro_step(self, base, off, pos):
        """(значение, новая позиция); off - смещение макроса в инструменте"""
        a = base + off
        ln, loop = self.d[a], self.d[a + 1]
        v = self.d[a + 2 + pos]
        pos += 1
        if pos >= ln:
            pos = loop if loop != 0xFF else ln - 1
        return v, pos

    def note_on(self, t):
        if t.pend_ins is not None:
            t.ins = t.pend_ins
            t.pend_ins = None
        base = self.ins_rec(t.ins)
        n16 = t.pend_note * 16
        if t.pend == "legato" and t.state == 1:
            if t.porta:
                t.target = n16
            else:
                t.cur = t.target = n16
        else:
            t.cur = t.target = n16
            t.state = 1
            t.regpos = t.arppos = t.pitpos = 0
            t.vib_cnt = self.d[base + 9]
            t.vib_phase = 0
            t.arp_phase = 0
            t.retrig_cnt = 0
            t.trem_phase = 0
        t.pend = None
        t.cut = t.cut_req + 1 if t.cut_req else 0

    def tone_tick(self, ch, t):
        if t.pend and self.tick == 1 + t.delay:
            self.note_on(t)
        if t.cut:
            t.cut -= 1
            if t.cut == 0 and t.state == 1:
                t.state, t.regpos = 2, 0
        if t.state == 0:
            return
        base = self.ins_rec(t.ins)
        d = self.d
        # Регистр
        if t.state == 1 and t.retrig:
            t.retrig_cnt += 1
            if t.retrig_cnt >= t.retrig:
                t.retrig_cnt = 0
                t.regpos = 0
        off = d[base + 13] if t.state == 1 else d[base + 14]
        v, t.regpos = self.macro_step(base, off, t.regpos)
        if t.bus_fx != 0xFF:
            v = (v & 0x30) | (t.bus_fx & 0x0F)
        if t.state == 1:
            if t.trem:
                if t.trem_phase >= (t.trem + 1) // 2:
                    v &= ~0x20
                t.trem_phase += 1
                if t.trem_phase >= t.trem:
                    t.trem_phase = 0
            if t.retrig and t.retrig_cnt == t.retrig - 1:
                v &= ~0x10
        if t.pend == "note":
            v &= ~0x10
        self.reg_out(ch, t, v)
        # Высота
        if t.cur != t.target:
            if t.cur < t.target:
                t.cur = min(t.cur + t.porta, t.target)
            else:
                t.cur = max(t.cur - t.porta, t.target)
        # Скольжение и макрос высоты двигают и цель: PORTA тянет только к ноте лиги
        if t.slide:
            t.cur += t.slide
            t.target += t.slide
        if d[base + 16]:
            dv, t.pitpos = self.macro_step(base, d[base + 16], t.pitpos)
            t.cur += s8(dv)
            t.target += s8(dv)
        t.cur = max(0, min(1535, t.cur))
        t.target = max(0, min(1535, t.target))
        a = 0
        if d[base + 15]:
            av, t.arppos = self.macro_step(base, d[base + 15], t.arppos)
            a = s8(av)
        if t.arp_fx:
            if t.arp_phase == 1:
                a += t.arp_fx >> 4
            elif t.arp_phase == 2:
                a += t.arp_fx & 15
            t.arp_phase = (t.arp_phase + 1) % 3
        vs, vd = (t.vib_fx >> 4, t.vib_fx & 15) if t.vib_fx else (d[base + 10], d[base + 11])
        vib = 0
        if vd:
            if t.vib_cnt:
                t.vib_cnt -= 1
            else:
                t.vib_phase = (t.vib_phase + vs) & 63
                vib = s8(VIB[vd * 64 + t.vib_phase])
        total = t.cur + (t.transpose + a) * 16 + s8(d[base + 12]) + t.det_fx + vib
        total = max(0, min(1535, total))
        p = period_of(total)
        if p != t.last_per:
            t.last_per = p
            self.write(CNT_ADDR[ch], p & 255)
            self.write(CNT_ADDR[ch], p >> 8)

    def step(self):
        """Один тик; False - мелодия кончилась"""
        if self.ended:
            return False
        if self.tick == 0:
            for ch in range(8):
                self.parse(ch)
            for i in range(2):
                if self.drum_pend[i] is not None:
                    self.write(13 + i, self.drum_pend[i] & 0x0F)
        for ch in range(5):
            self.tone_tick(ch, self.tone[ch])
        if self.tick == 1:
            for i in range(2):
                if self.drum_pend[i] is not None:
                    v = (self.drum_pend[i] & 0x0F) | 0x10
                    self.drum_preset[i] = self.drum_pend[i] & 0x0F
                    self.drum_pend[i] = None
                    self.write(13 + i, v)
        self.ticks += 1
        self.tick += 1
        if self.tick >= self.speed:
            self.tick = 0
            self.row += 1
            if self.brk or self.jump is not None or self.row >= self.frame_rows:
                f = self.jump if self.jump is not None else self.frame + 1
                self.jump, self.brk = None, False
                if f >= self.frames:
                    if self.loop is None:
                        self.ended = True
                        return False
                    f = self.loop
                self.load_frame(f)
        return True


def main():
    data = open(sys.argv[1], "rb").read()
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 7
    p = Player(data)
    while p.ticks < limit and p.step():
        pass
    secs = sum(0 for _ in [])
    print("тиков %d, записей %d, кадр %d" % (p.ticks, len(p.writes), p.frame))


if __name__ == "__main__":
    main()
