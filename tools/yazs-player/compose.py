# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Партитура -> файл .YZ (FORMAT.md).

Мелодия пишется на Python: инструменты, голоса на языке вроде MML, ударные,
управляющие события (темп, подписи). Song.build() раскладывает все по строкам,
режет на кадры, убирает одинаковые паттерны и собирает файл.

Язык голоса (один тоновый канал):
    o4 > <        октава (C4 = до первой октавы), выше, ниже
    l8 l8. l12    длительность по умолчанию (12 - восьмая триоль), точки
    c d e f g a b нота, затем # + (диез) или - (бемоль), длительность, точки
    c4^16         связка: к длительности прибавить еще
    r4            пауза (ноте - отпускание)
    _e8           лига: без нового удара, со скольжением при PORTA
    q6            гейт: нота отпускается через 6/8 длительности (q8 - держать)
    @name         инструмент
    {vib 0x46}    эффект в начале следующей ноты/паузы (имена - EFFECTS)
    [ ... ]3      повтор
    |             черта такта: проверка, что такт полон
    ; ...         комментарий до конца строки
"""

import math
import re

import tables

EFFECTS = {"arp": 0, "su": 1, "sd": 2, "porta": 3, "vib": 4, "det": 5, "bus": 6,
           "trem": 7, "delay": 8, "cut": 9, "speed": 10, "tempo": 11, "jump": 12,
           "text": 13, "tr": 14, "retrig": 15, "break": 16}

CMD_OFF, CMD_CUT, CMD_INSTR, CMD_LEGATO, CMD_FX = 0x60, 0x61, 0x62, 0x63, 0x64


class Instrument(object):
    def __init__(self, name, label, reg, reg_loop=0xFF, rel=None, arp=None, arp_loop=0xFF,
                 pitch=None, pitch_loop=0xFF, vib=(0, 0, 0), detune=0, meter=(12, 8, 6)):
        self.name = name
        self.label = label
        self.reg = list(reg)
        self.reg_loop = reg_loop
        if rel is None:
            # Отпустить: полосы и D3 те же, огибающая снята
            rel = [self.reg[-1] & 0x0F]
        self.rel = list(rel)
        self.arp = list(arp) if arp else None
        self.arp_loop = arp_loop
        self.pitch = list(pitch) if pitch else None
        self.pitch_loop = pitch_loop
        self.vib = vib
        self.detune = detune
        self.meter = meter            # пик, удержание (0-15), спад за тик (1/16)

    def encode(self):
        out = bytearray(tables.agat(self.label, 8))
        out += bytes([0, self.vib[0], self.vib[1], self.vib[2], self.detune & 0xFF])
        hdr = len(out)                # смещения макросов: 13..16
        out += bytes(4)
        out += bytes([(self.meter[0] << 4) | self.meter[1], self.meter[2]])

        def macro(vals, loop):
            pos = len(out)
            out.extend([len(vals), loop] + [v & 0xFF for v in vals])
            return pos
        out[hdr] = macro(self.reg, self.reg_loop)
        out[hdr + 1] = macro(self.rel, 0xFF)
        out[hdr + 2] = macro(self.arp, self.arp_loop) if self.arp else 0
        out[hdr + 3] = macro(self.pitch, self.pitch_loop) if self.pitch else 0
        assert len(out) < 256, self.name
        return bytes(out)


NOTE_BASE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


class Song(object):
    def __init__(self, title, subtitle="", bpm=120, rows_per_beat=4, beats_per_bar=4,
                 frame_bars=1, speed=None, loop=None):
        self.title = title
        self.subtitle = subtitle
        self.rpb = rows_per_beat
        self.bar_rows = rows_per_beat * beats_per_bar
        self.frame_rows = self.bar_rows * frame_bars
        self.whole = rows_per_beat * 4            # строк в целой ноте
        self.speed, self.n = self.pick_speed(bpm, speed)
        self.bpm = bpm
        self.loop = loop
        self.instruments = []
        self.inames = {}
        self.texts = []
        # строки каналов: канал -> {строка: [команды]}
        self.rows = [dict() for _ in range(8)]
        self.end_row = 0

    # --- время ---

    def pick_speed(self, bpm, speed=None):
        rows_per_s = bpm * self.rpb / 60.0
        best = None
        for s in ([speed] if speed else range(2, 30)):
            tick = rows_per_s * s
            if not speed and not (75 <= tick <= 135):
                continue
            n = int(round(tables.TIMER_CLOCK / tick))
            err = abs(tables.TIMER_CLOCK / n / s - rows_per_s) / rows_per_s
            # тик выше 100 Гц - меньше запас процессора на тик: дороже, чем
            # полпроцента темпа
            score = err + max(0.0, tick - 100) / 200.0 + abs(tick - 100) / 5000.0
            if best is None or score < best[0]:
                best = (score, s, n)
        if best is None:
            raise ValueError("темп %g не уложить в тики" % bpm)
        return best[1], best[2]

    def tempo_n(self, bpm, speed=None):
        """N таймера для темпа при текущей (или заданной) скорости"""
        s = speed or self.speed
        return int(round(tables.TIMER_CLOCK * 60.0 / (bpm * self.rpb * s)))

    def bar(self, b, beat=0, row=0):
        return b * self.bar_rows + int(round(beat * self.rpb)) + row

    def set_tempo(self, row, bpm, speed=None):
        """Темп с этой строки: скорость строки (SPEED) подбирается так, чтобы тик
        был около 100 Гц, и новый N таймера (TEMPO)"""
        if speed is None:
            speed = self.pick_speed(bpm)[0]
        n = self.tempo_n(bpm, speed)
        if not 20 <= n <= 255:
            raise ValueError("темп %g: N = %d вне 20..255" % (bpm, n))
        self.ctrl(row, "speed", speed)
        self.ctrl(row, "tempo", n)
        return speed

    def ritard(self, row0, row1, bpm0, bpm1, every=None):
        """Плавная смена темпа от bpm0 к bpm1 на строках row0..row1 (каждые every
        строк) при скорости строки, подобранной для bpm0"""
        every = every or max(1, self.rpb // 2)
        steps = max(1, (row1 - row0) // every)
        speed = None
        for i in range(steps + 1):
            r = row0 + i * every
            if r > row1:
                break
            speed = self.set_tempo(r, bpm0 + (bpm1 - bpm0) * i / float(steps), speed)

    def swing(self, row0, row1, long_ticks, short_ticks):
        """Свинг восьмых при 4 строках на долю: строки пар долгой/короткой восьмой"""
        assert self.rpb == 4
        pat = [long_ticks // 2, long_ticks - long_ticks // 2,
               short_ticks // 2, short_ticks - short_ticks // 2]
        for r in range(row0, row1):
            self.ctrl(r, "speed", pat[(r - row0) % 4])
        self.ctrl(row1, "speed", self.speed)

    # --- инструменты и подписи ---

    def instrument(self, name, label, reg, **kw):
        ins = Instrument(name, label, reg, **kw)
        self.inames[name] = len(self.instruments)
        self.instruments.append(ins)
        return ins

    def text(self, row, s):
        self.texts.append(tables.agat(s))
        self.ctrl(row, "text", len(self.texts) - 1)

    def ctrl(self, row, fx, p):
        self.put(7, row, (CMD_FX + EFFECTS[fx], p & 0xFF))

    def put(self, ch, row, cmd):
        self.rows[ch].setdefault(row, []).append(cmd)
        self.end_row = max(self.end_row, row + 1)

    # --- голос ---

    def length(self, num, dots):
        if 64 % num and num not in (3, 6, 12, 24, 48):
            raise ValueError("длительность %d" % num)
        rows = self.whole / float(num)
        add = rows
        for _ in range(dots):
            add /= 2
            rows += add
        if abs(rows - round(rows)) > 1e-6:
            raise ValueError("1/%d%s не укладывается в строки (строк на долю %d)" % (
                num, "." * dots, self.rpb))
        return int(round(rows))

    def voice(self, ch, text, start=0, octave=4, length=4, gate=8, transpose=0):
        """Тоновый канал 0-4 (или ударный, если ноты заданы числами); возвращает строку конца"""
        tokens = tokenize(text)
        tokens = expand_repeats(tokens)
        row = start
        deflen = self.length(length, 0)
        last_note_end = None
        pending = []                 # эффекты и инструмент до ближайшей ноты
        bar_start = start
        prev = None                  # (строка начала, длина) последней ноты - для связки
        for kind, val in tokens:
            if kind == "oct":
                octave = val
            elif kind == "up":
                octave += 1
            elif kind == "down":
                octave -= 1
            elif kind == "len":
                deflen = self.length(*val)
            elif kind == "gate":
                gate = val
            elif kind == "instr":
                if val not in self.inames:
                    raise ValueError("нет инструмента %s" % val)
                pending.append((CMD_INSTR, self.inames[val]))
            elif kind == "fx":
                name, p = val
                if name not in EFFECTS:
                    raise ValueError("эффект %s" % name)
                pending.append((CMD_FX + EFFECTS[name], p & 0xFF))
            elif kind == "bar":
                if (row - bar_start) % self.bar_rows:
                    raise ValueError("такт не полон: строка %d (такт %d), канал %d, около %r" % (
                        row, row // self.bar_rows, ch, text[:40]))
            elif kind == "tie":
                if prev is None:
                    raise ValueError("связка без ноты")
                n = self.length(*val)
                self.extend(ch, prev, n, gate)
                prev = (prev[0], prev[1] + n, prev[2])
                row += n
            elif kind in ("note", "rest"):
                n = self.length(*val[1]) if val[1] else deflen
                for c in pending:
                    self.put(ch, row, c)
                pending = []
                if kind == "rest":
                    self.put(ch, row, (CMD_OFF,))
                    prev = None
                else:
                    legato, pitch = val[0]
                    if isinstance(pitch, int):
                        note = pitch
                    else:
                        letter, acc = pitch
                        note = (octave * 12) + NOTE_BASE[letter] + acc + transpose
                    if not 0 <= note < 96:
                        raise ValueError("нота вне 0..95: %d" % note)
                    self.put(ch, row, (CMD_LEGATO, note) if legato else (note,))
                    self.gate_off(ch, row, n, gate)
                    prev = (row, n, gate)
                row += n
        for c in pending:
            self.put(ch, row, c)
        self.end_row = max(self.end_row, row)
        return row

    def gate_off(self, ch, row, n, gate):
        if gate < 8:
            off = row + max(1, int(math.floor(n * gate / 8.0)))
            if off < row + n:
                self.put(ch, off, (CMD_OFF,))

    def extend(self, ch, prev, n, gate):
        start, ln, g = prev
        if g < 8:
            # Отпускание старой длины снять и поставить по новой
            off = start + max(1, int(math.floor(ln * g / 8.0)))
            cmds = self.rows[ch].get(off, [])
            if (CMD_OFF,) in cmds:
                cmds.remove((CMD_OFF,))
            self.gate_off(ch, start, ln + n, g)

    def drums(self, ch, text, kit, start=0, length=8):
        """Ударный канал 5/6: буквы из kit -> пресет (D0-D3), r - пауза"""
        # Пересобираем в голос с нотами-пресетами: k8 -> =8:8
        conv = []
        for tok in re.findall(r"l\d+\.*|[A-Za-z]\d*\.*|\||\[|\]\d*|\^\d+\.*|\{[^}]*\}",
                              strip_comments(text)):
            if tok.startswith("l") and len(tok) > 1 and tok[1].isdigit():
                conv.append(tok)
            elif tok[0] in kit:
                conv.append("=%d%s" % (kit[tok[0]], (":" + tok[1:]) if tok[1:] else ""))
            else:
                conv.append(tok)
        return self.voice(ch, " ".join(conv), start=start, length=length)

    # --- сборка ---

    def build(self):
        total = max(self.end_row, 1)
        frames = (total + self.frame_rows - 1) // self.frame_rows
        patterns = []
        pindex = {}
        order = []
        for f in range(frames):
            a = f * self.frame_rows
            rows = min(self.frame_rows, total - a)
            entry = [rows]
            for ch in range(8):
                data = self.encode_pattern(ch, a, rows)
                if data not in pindex:
                    pindex[data] = len(patterns)
                    patterns.append(data)
                entry.append(pindex[data])
            order.append(entry)
        if len(patterns) > 256:
            raise ValueError("паттернов %d > 256" % len(patterns))

        body = bytearray()
        HDR = 80

        def place(blob):
            pos = HDR + len(body)
            body.extend(blob)
            return pos
        o_orders = place(bytes(b for e in order for b in e))
        o_ptab = HDR + len(body)
        body.extend(bytes(2 * len(patterns)))
        o_itab = HDR + len(body)
        body.extend(bytes(2 * len(self.instruments)))
        o_ttab = HDR + len(body)
        body.extend(bytes(2 * max(1, len(self.texts))))
        for i, p in enumerate(patterns):
            pos = place(p)
            body[o_ptab - HDR + 2 * i:o_ptab - HDR + 2 * i + 2] = bytes([pos & 255, pos >> 8])
        for i, ins in enumerate(self.instruments):
            pos = place(ins.encode())
            body[o_itab - HDR + 2 * i:o_itab - HDR + 2 * i + 2] = bytes([pos & 255, pos >> 8])
        for i, t in enumerate(self.texts):
            pos = place(t + b"\x00")
            body[o_ttab - HDR + 2 * i:o_ttab - HDR + 2 * i + 2] = bytes([pos & 255, pos >> 8])

        secs = int(round(self.duration(order)))
        loop = self.loop if self.loop is not None else 0xFF
        hdr = bytearray(b"YZ") + bytes([1, 1 if self.loop is not None else 0])
        hdr += tables.agat(self.title, 30)
        hdr += tables.agat(self.subtitle, 30)
        hdr += bytes([secs & 255, secs >> 8, self.n, self.speed, frames, loop])
        for o in (o_orders, o_ptab, o_itab, o_ttab, 0):
            hdr += bytes([o & 255, o >> 8])
        assert len(hdr) == HDR
        data = bytes(hdr + body)
        if len(data) > 0x8000:
            raise ValueError("файл %d байт > 32 КБ" % len(data))
        self.stats = {"frames": frames, "patterns": len(patterns), "bytes": len(data),
                      "seconds": secs, "speed": self.speed, "n": self.n}
        return data

    def encode_pattern(self, ch, a, rows):
        out = bytearray()
        cmd_rows = sorted(r - a for r in self.rows[ch] if a <= r < a + rows)
        cmd_rows_set = set(cmd_rows)
        r = 0
        # Строка 0 всегда начинает паттерн (хотя бы концом строки)
        points = sorted(set([0] + cmd_rows))
        for i, r in enumerate(points):
            for c in self.ordered(self.rows[ch].get(a + r, [])) if r in cmd_rows_set else []:
                out.extend(c)
            nxt = points[i + 1] if i + 1 < len(points) else rows
            skip = nxt - r - 1
            while skip > 127:
                # 127 строк пропуска, следующая читается пустой
                out.append(0xFF)
                skip -= 128
            out.append(0x80 + skip)
        return bytes(out)

    @staticmethod
    def ordered(cmds):
        """Инструмент и эффекты - до ноты; отпускание, если в строке есть нота, - не нужно"""
        notes = [c for c in cmds if c[0] < 0x60 or c[0] == CMD_LEGATO]
        rest = [c for c in cmds if c not in notes]
        if notes:
            rest = [c for c in rest if c[0] != CMD_OFF]
        else:
            # Несколько отпусканий в строке - одно
            seen = False
            out = []
            for c in rest:
                if c[0] == CMD_OFF:
                    if seen:
                        continue
                    seen = True
                out.append(c)
            rest = out
        return [c for c in rest if c[0] != CMD_OFF] + [c for c in rest if c[0] == CMD_OFF] + notes[-1:]

    def duration(self, order):
        speed, n = self.speed, self.n
        t = 0.0
        for f, e in enumerate(order):
            for r in range(e[0]):
                for c in self.rows[7].get(f * self.frame_rows + r, []):
                    if c[0] == CMD_FX + EFFECTS["speed"]:
                        speed = c[1]
                    elif c[0] == CMD_FX + EFFECTS["tempo"]:
                        n = c[1]
                t += speed * n / float(tables.TIMER_CLOCK)
        return t


def strip_comments(text):
    return re.sub(r";[^\n]*", "", text)


TOKEN = re.compile(r"""
    (?P<oct>o\d) | (?P<up>>) | (?P<down><) |
    (?P<len>l\d+\.*) | (?P<gate>q\d) |
    (?P<instr>@[A-Za-z0-9_]+) |
    (?P<fx>\{\s*[a-z]+\s+[-+]?(?:0x[0-9a-fA-F]+|\d+)\s*\}) |
    (?P<bar>\|) | (?P<rep_open>\[) | (?P<rep_close>\]\d*) |
    (?P<tie>\^\d+\.*) |
    (?P<note>_?(?:[a-g][#+-]?\d*\.*|=\d+(?::\d+\.*)?)) |
    (?P<rest>r\d*\.*) |
    (?P<space>\s+)
""", re.X)


def parse_len(s):
    m = re.match(r"(\d*)(\.*)$", s)
    if not m.group(1):
        return None
    return (int(m.group(1)), len(m.group(2)))


def tokenize(text):
    text = strip_comments(text)
    pos = 0
    out = []
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ValueError("не разобрать: %r" % text[pos:pos + 30])
        pos = m.end()
        k = m.lastgroup
        s = m.group(k)
        if k == "space":
            continue
        if k == "oct":
            out.append(("oct", int(s[1:])))
        elif k in ("up", "down", "bar", "rep_open"):
            out.append((k, None))
        elif k == "rep_close":
            out.append((k, int(s[1:]) if len(s) > 1 else 2))
        elif k == "len":
            out.append(("len", parse_len(s[1:])))
        elif k == "gate":
            out.append(("gate", int(s[1:])))
        elif k == "instr":
            out.append(("instr", s[1:]))
        elif k == "fx":
            name, p = s.strip("{} ").split()
            out.append(("fx", (name, int(p, 0))))
        elif k == "tie":
            out.append(("tie", parse_len(s[1:])))
        elif k == "rest":
            out.append(("rest", (None, parse_len(s[1:]))))
        elif k == "note":
            legato = s.startswith("_")
            s = s.lstrip("_")
            if s.startswith("="):
                # =N - номер ноты (пресет ударного), =N:8 - с длительностью
                m2 = re.match(r"=(\d+)(?::(\d+\.*))?$", s)
                out.append(("note", ((legato, int(m2.group(1))), parse_len(m2.group(2) or ""))))
                continue
            letter = s[0]
            rest = s[1:]
            acc = 0
            if rest[:1] in ("#", "+"):
                acc, rest = 1, rest[1:]
            elif rest[:1] == "-":
                acc, rest = -1, rest[1:]
            out.append(("note", ((legato, (letter, acc)), parse_len(rest))))
    return out


def expand_repeats(tokens):
    out = []
    stack = [out]
    for t in tokens:
        if t[0] == "rep_open":
            stack.append([])
        elif t[0] == "rep_close":
            body = stack.pop()
            stack[-1].extend(body * t[1])
        else:
            stack[-1].append(t)
    if len(stack) != 1:
        raise ValueError("непарные [ ]")
    return out
