# Run from the directory with cases.json, table.pkl and the logs of the
# 235-case runs (gB..gJ without the flip-flop, oB1.. with it, see FLOP).
# Summary workbook of the К1801ВМ1 timing study: every measured instruction,
# its bus cycles, the Verilog simulation, the model and the real machines.
import json, sys, pickle, collections
import os as _os
HERE = _os.path.dirname(_os.path.abspath(__file__))
sys.argv = sys.argv[:1] + ['x']
import model
from extract import form_key
import gen
from openpyxl import Workbook
from openpyxl.styles import Font, Alignment, PatternFill, Border, Side
from openpyxl.utils import get_column_letter
from openpyxl.comments import Comment

OUT = _os.path.join(HERE, '..', '..', 'docs-external', 'BK', 'speed', 'vm1-timing-study.xlsx')
P = dict(win=1333, sr=250, sw=400, sm=700, lat=550, fm=2)

COLS = ['B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J']
CNAME = {'B': 'БК0010 3 МГц: код ОЗУ, данные ОЗУ', 'C': '3 МГц: код статика, данные ОЗУ',
         'D': '3 МГц: код статика, данные статика',
         'E': 'БК0011 4 МГц: код ОЗУ, данные ОЗУ', 'F': '4 МГц: код статика, данные ОЗУ',
         'G': '4 МГц: код статика, данные статика',
         'H': '6 МГц: код ОЗУ, данные ОЗУ', 'I': '6 МГц: код статика, данные ОЗУ',
         'J': '6 МГц: код статика, данные статика'}
XL = {c: i for i, c in enumerate(COLS)}

# ---------------------------------------------------------------- data
cases = json.load(open('cases.json'))
real11 = {c['text']: c['real11'] for c in json.load(open(_os.path.join(HERE, 'bk0011.json')))}

exec(open(_os.path.join(HERE, 'analyze.py'), encoding='utf-8').read().split('def main')[0])
def per(reads, L):
    """period over the last L copies: L a multiple of the pattern (2 at 3 and 6 MHz, 3 at 4 MHz)"""
    out = []
    for c in cases:
        try: t = [reads[a][0] for a in c['copies']]
        except KeyError: out.append(None); continue
        out.append((t[11] - t[11 - L]) / float(L))
    return out

def simrun(run, col):
    r = load(run) if run else None
    return per(r, 6 if col in 'EFG' else 8) if r else [None] * len(cases)

sim = {c: simrun('g' + c, c) for c in COLS}
# with the flip-flop that takes RPLY into the processor on its clock (both БК have
# one); runs of the model with rply_ack[1] restored, which gives the same times
FLOP = {'B': 'oB1', 'C': 'oC1', 'D': 'oD1', 'E': 'oE1', 'F': 'oF1', 'G': 'gG',
        'H': 'oH1', 'I': 'oI1', 'J': 'gJ'}
sim2 = {c: simrun(FLOP[c], c) for c in COLS}

def model_val(key, col):
    hz, sc, sd = model.CONF[col]
    r = model.Reply(hz, P['win'], P['sr'], P['sw'], P['sm'], P['lat'])
    return model.steady(key, sc, sd, r, fast=model.FastCard(P['fm']))

KIND = {'R': 'чтение (DATI)', 'W': 'запись (DATO)', 'M': 'чтение-модификация-запись (DATIO)'}
ROLE = {'I': 'поток команд', 'D': 'данные', 'N': 'IAKO'}

def timeline(key, col, iterations=1):
    """per step: (sync, strobe, reply, release) in clocks from the instruction start,
    for the given iteration of the steady state"""
    hz, sc, sd = model.CONF[col]
    r = model.Reply(hz, P['win'], P['sr'], P['sw'], P['sm'], P['lat'])
    fast = model.FastCard(P['fm'])
    steps = model.table[key]['steps']; tail = model.table[key]['tail']
    reps = [(r if (sc if s[1] == 'I' else sd) else fast) for s in steps]
    ch = model.Chain()
    for k in range(48):            # settle the phase
        ch.run(key, reps)
    out = []
    for it in range(iterations):
        start = ch.start; cur = start; rows = []
        for s, rep in zip(steps, reps):
            kind, role, gap, strobe, r2w = s
            sync = cur + gap * model.TPH
            st = sync + strobe * model.TPH
            if kind == 'M':
                rp = model.reply_time(st, 'R', False, rep)
                rel = rp + model.RELEASE * model.TPH
                st2 = rel + r2w * model.TPH
                rp2 = model.reply_time(st2, 'W', True, rep)
                cur = rp2 + model.RELEASE * model.TPH
                rows.append((sync, st, rp, rel, st2, rp2, cur))
            else:
                rp = model.reply_time(st, kind, False, rep)
                cur = rp + model.RELEASE * model.TPH
                rows.append((sync, st, rp, cur, None, None, None))
        ch.run(key, reps)
        f = lambda v: None if v is None else (v - start) / model.TPC
        out.append(([tuple(f(x) for x in row) for row in rows], (ch.start - start) / model.TPC))
    return out

rows = []
for i, c in enumerate(cases):
    key = model.case_key(c['text'])
    words = gen.assemble(c['text'])
    codes = ' '.join('%06o' % w if isinstance(w, int) else 'след.' for w in words)
    real = {}
    if 'xlsx' in c:
        real = {col: c['xlsx'][XL[col]] for col in COLS}
        src = 'таблица BK0010-11-instructions-speed.xlsx'
    else:
        real = {'B': c['real']}
        if c['text'] in real11: real['E'] = real11[c['text']]
        src = '45com-lo: фото с БК0010' + (' и БК0011М' if 'E' in real else '')
    mod = {col: round(model_val(key, col), 3) for col in COLS}
    sv = {col: (None if sim[col][i] is None else round(sim[col][i], 3)) for col in COLS}
    sv2 = {col: (None if sim2[col][i] is None else round(sim2[col][i], 3)) for col in COLS}
    rows.append(dict(name=c['name'] if 'xlsx' in c else c['text'].replace(' next', ' .+2').replace('@#fm', '@#адр'),
                     key=key, codes=codes, src=src, real=real, mod=mod, sim=sv, sim2=sv2,
                     steps=model.table[key]['steps'], tail=model.table[key]['tail']))

def near(a, b): return a is not None and b is not None and abs(a - b) < 0.05

def comment(r):
    notes = []
    real, mod = r['real'], r['mod']
    sv = {c: (r['sim2'][c] if r['sim2'][c] is not None else r['sim'][c]) for c in COLS}
    miss_m = [c for c in real if not near(mod[c], real[c])]
    miss_s = [c for c in real if sv[c] is not None and not near(sv[c], real[c])]
    is_add = r['name'].startswith('ADD')
    datio_dst = any(s[0] == 'M' for s in r['steps'])
    if not miss_m:
        notes.append('Модель = реальная машина во всех %d замерах.' % len(real))
    else:
        for c in miss_m:
            why = ''
            if 'D' in real and c in ('B', 'C') and real[c] < real['D'] - 0.01:
                why = ' — в таблице slow меньше fast, ошибка записи в таблице'
            elif c == 'C' and is_add and datio_dst:
                why = ' — колонка «код в статике» у ADD в DATIO: известное систематическое расхождение (касается платы статики)'
            elif c in ('J', 'I', 'G'):
                why = ' — на 6 МГц плата статики сама отвечает примерно на такт позже на обращение; модель считает ее ответ мгновенным (платы в эмуляторе нет)'
            elif c in ('C', 'F'):
                why = ' — код в статике, данные в ОЗУ: сочетание с платой статики, которой в эмуляторе нет; на БК без платы не влияет'
            notes.append('Модель ≠ реал в %s: %g против %g%s.' % (c, mod[c], real[c], why))
    if miss_s:
        slow = [c for c in miss_s if c in ('B', 'C', 'E', 'F')]
        fast = [c for c in miss_s if c in ('D', 'G')]
        if 'J' in miss_s or 'I' in miss_s:
            notes.append('Verilog ≠ реал в %s: на 6 МГц плата статики отвечает позже, в стенде ответ статики мгновенный.'
                         % ','.join(c for c in ('I', 'J') if c in miss_s))
            miss_s = [c for c in miss_s if c not in ('I', 'J')]
        other = [c for c in miss_s if c not in slow + fast]
        if slow:
            notes.append('Verilog (с триггером RPLY) ≠ реал в %s: на 3/4 МГц стенд и с триггером еще быстрее железа на 1-3 окна у части форм (причина не найдена).' % ','.join(slow))
        if fast:
            notes.append('Verilog ≠ реал в %s: %s' % (','.join(fast),
                         'у платы статики запись в DATIO на такт дольше (в модели rmw_extra = 2).' if datio_dst else 'расхождение на статике.'))
        if other:
            notes.append('Verilog ≠ реал в %s.' % ','.join(other))
    elif any(sv[c] is not None for c in real):
        notes.append('Verilog (с триггером RPLY) = реал везде, где есть замер.')
    return ' '.join(notes)

# ---------------------------------------------------------------- workbook
FONT = 'Arial'
f_norm = Font(name=FONT, size=10)
f_bold = Font(name=FONT, size=10, bold=True)
f_head = Font(name=FONT, size=10, bold=True, color='FFFFFF')
f_title = Font(name=FONT, size=14, bold=True)
f_blue = Font(name=FONT, size=10, color='0000FF')
fill_head = PatternFill('solid', fgColor='305496')
fill_grp = {'real': PatternFill('solid', fgColor='E2EFDA'), 'sim': PatternFill('solid', fgColor='DDEBF7'),
            'sim2': PatternFill('solid', fgColor='BDD7EE'),
            'mod': PatternFill('solid', fgColor='FFF2CC'), 'cmp': PatternFill('solid', fgColor='F2F2F2')}
fill_bad = PatternFill('solid', fgColor='F8CBAD')
thin = Side(style='thin', color='BFBFBF')
box = Border(left=thin, right=thin, top=thin, bottom=thin)
wrap = Alignment(wrap_text=True, vertical='top')
center = Alignment(horizontal='center', vertical='center', wrap_text=True)

wb = Workbook()

# ---- sheet 1: summary
ws = wb.active
ws.title = 'Сводка'
fixed = ['№', 'Команда', 'Источник замера', 'Коды (восьм.)', 'Форма в таблице эмулятора',
         'Этапы исполнения (циклы магистрали)', 'Циклов шины', 'Хвост, полутакты']
ws.cell(row=1, column=1, value='Время команд К1801ВМ1, тактов процессора на команду (установившийся режим)').font = f_title
for j, h in enumerate(fixed, 1):
    ws.cell(row=3, column=j, value=h)
    ws.merge_cells(start_row=2, start_column=j, end_row=3, end_column=j) if False else None
col = len(fixed) + 1
colmap = {}
for c in COLS:
    ws.cell(row=2, column=col, value=CNAME[c] + ' (колонка %s таблицы)' % c)
    ws.merge_cells(start_row=2, start_column=col, end_row=2, end_column=col + 5)
    for k, (h, g) in enumerate((('Реал', 'real'), ('Verilog без триггера', 'sim'), ('Verilog + триггер RPLY', 'sim2'),
                                ('Модель', 'mod'), ('Модель − реал', 'd'), ('Совпало', 'ok'))):
        ws.cell(row=3, column=col + k, value=h)
        colmap[(c, g)] = col + k
    col += 6
ws.cell(row=3, column=col, value='Комментарий: что совпало, что нет')
last_col = col
for rr in (2, 3):
    for j in range(1, last_col + 1):
        cell = ws.cell(row=rr, column=j)
        cell.font = f_head; cell.fill = fill_head; cell.alignment = center; cell.border = box

def steps_text(steps, tail):
    parts = []
    for n, s in enumerate(steps, 1):
        kind, role, gap, strobe, r2w = s
        what = 'предвыборка след. команды' if n == len(steps) else ROLE[role]
        t = '%d) %s, %s: SYNC через %d, строб +%d' % (n, KIND[kind].split(' ')[0], what, gap, strobe)
        if kind == 'M': t += ', запись через %d после чтения' % r2w
        parts.append(t)
    if tail: parts.append('хвост %d' % tail)
    return '\n'.join(parts)

r0 = 4
for i, r in enumerate(rows):
    rr = r0 + i
    vals = [i + 1, r['name'], r['src'], r['codes'], repr(r['key']), steps_text(r['steps'], r['tail']),
            len(r['steps']), r['tail']]
    for j, v in enumerate(vals, 1):
        cell = ws.cell(row=rr, column=j, value=v); cell.font = f_norm; cell.border = box
        cell.alignment = wrap
    for c in COLS:
        creal, csim, csim2, cmod = colmap[(c, 'real')], colmap[(c, 'sim')], colmap[(c, 'sim2')], colmap[(c, 'mod')]
        for cc, v, g in ((creal, r['real'].get(c), 'real'), (csim, r['sim'][c], 'sim'),
                         (csim2, r['sim2'][c], 'sim2'), (cmod, r['mod'][c], 'mod')):
            cell = ws.cell(row=rr, column=cc, value=v)
            cell.font = f_blue if g == 'real' else f_norm
            cell.fill = fill_grp[g]; cell.border = box; cell.number_format = '0.00'
        R = get_column_letter(creal) + str(rr); M = get_column_letter(cmod) + str(rr)
        d = ws.cell(row=rr, column=colmap[(c, 'd')], value='=IF(%s="","",%s-%s)' % (R, M, R))
        d.number_format = '+0.00;-0.00;0'; d.font = f_norm; d.border = box; d.fill = fill_grp['cmp']
        ok = ws.cell(row=rr, column=colmap[(c, 'ok')], value='=IF(%s="","",IF(ABS(%s-%s)<0.05,"да","нет"))' % (R, M, R))
        ok.font = f_norm; ok.border = box; ok.alignment = center; ok.fill = fill_grp['cmp']
    cm = ws.cell(row=rr, column=last_col, value=comment(r)); cm.font = f_norm; cm.alignment = wrap; cm.border = box
last_row = r0 + len(rows) - 1

from openpyxl.formatting.rule import FormulaRule
for c in COLS:
    L = get_column_letter(colmap[(c, 'ok')])
    ws.conditional_formatting.add('%s%d:%s%d' % (L, r0, L, last_row),
                                  FormulaRule(formula=['%s%d="нет"' % (L, r0)], fill=fill_bad))
    # Verilog cells red when they differ from the real machine
    R = get_column_letter(colmap[(c, 'real')])
    for g in ('sim', 'sim2'):
        S = get_column_letter(colmap[(c, g)])
        ws.conditional_formatting.add('%s%d:%s%d' % (S, r0, S, last_row),
                                      FormulaRule(formula=['AND(%s%d<>"",%s%d<>"",ABS(%s%d-%s%d)>=0.05)' % (R, r0, S, r0, S, r0, R, r0)],
                                                  fill=fill_bad))

widths = {1: 5, 2: 20, 3: 22, 4: 16, 5: 18, 6: 46, 7: 8, 8: 8}
for j, w in widths.items(): ws.column_dimensions[get_column_letter(j)].width = w
for j in range(len(fixed) + 1, last_col): ws.column_dimensions[get_column_letter(j)].width = 9
ws.column_dimensions[get_column_letter(last_col)].width = 70
ws.row_dimensions[2].height = 42
ws.row_dimensions[3].height = 42
ws.freeze_panes = ws.cell(row=r0, column=3)
ws.auto_filter.ref = 'A3:%s%d' % (get_column_letter(last_col), last_row)
ws.cell(row=2, column=6).comment = Comment(
    'Шаблон формы из симуляции кристалла (fast/fast): от отпускания выборки собственного кода '
    'до предвыборки следующей команды. Промежутки и смещения - в полутактах процессора.', 'eCat3')

# ---- sheet 2: stages
st = wb.create_sheet('Этапы')
heads = ['№', 'Команда', 'Этап', 'Цикл', 'Что читается/пишется', 'SYNC после отпускания пред., полутакты',
         'Строб после SYNC, полутакты', 'Запись после чтения (DATIO), полутакты']
tcfg = [('D', 'статика 3 МГц'), ('B', 'БК0010 3 МГц'), ('H', '6 МГц ОЗУ')]
for c, nm in tcfg:
    heads += ['%s: SYNC' % nm, '%s: строб' % nm, '%s: RPLY' % nm, '%s: отпускание' % nm]
heads += ['БК0011М 4 МГц: длительность этапа в 3 соседних командах (фаза окна)']
for j, h in enumerate(heads, 1):
    cell = st.cell(row=1, column=j, value=h); cell.font = f_head; cell.fill = fill_head; cell.alignment = center; cell.border = box
st.cell(row=2, column=1, value='Времена - в тактах процессора от начала команды (отпускания ее собственной выборки кода), установившийся режим. '
        'Для DATIO: RPLY и отпускание - второй (записывающей) половины.').font = Font(name=FONT, size=9, italic=True)
st.merge_cells(start_row=2, start_column=1, end_row=2, end_column=len(heads))
rr = 3
for i, r in enumerate(rows):
    key = r['key']
    tl = {c: timeline(key, c)[0] for c, _ in tcfg}
    e3 = timeline(key, 'E', 3)
    steps = r['steps']
    for n, s in enumerate(steps):
        kind, role, gap, strobe, r2w = s
        what = 'предвыборка кода следующей команды' if n == len(steps) - 1 else ROLE[role]
        vals = [i + 1, r['name'], n + 1, KIND[kind], what, gap, strobe, r2w if kind == 'M' else None]
        for c, _ in tcfg:
            row = tl[c][0][n]
            if kind == 'M':
                vals += [row[0], row[1], row[5], row[6]]
            else:
                vals += [row[0], row[1], row[2], row[3]]
        durs = []
        for it in range(3):
            rw = e3[it][0][n]
            prev = 0 if n == 0 else (e3[it][0][n - 1][6] if e3[it][0][n - 1][6] is not None else e3[it][0][n - 1][3])
            end = rw[6] if rw[6] is not None else rw[3]
            durs.append('%.2f' % (end - prev))
        vals.append(' / '.join(durs))
        for j, v in enumerate(vals, 1):
            cell = st.cell(row=rr, column=j, value=v); cell.font = f_norm; cell.border = box
            if isinstance(v, float): cell.number_format = '0.0'
        rr += 1
    # the total of the instruction
    tot = ['', r['name'], 'итого', 'такты на команду', '', None, None, None]
    for c, _ in tcfg: tot += [None, None, None, tl[c][1]]
    tot.append(' / '.join('%.2f' % e3[it][1] for it in range(3)))
    for j, v in enumerate(tot, 1):
        cell = st.cell(row=rr, column=j, value=v); cell.font = f_bold; cell.border = box
        cell.fill = PatternFill('solid', fgColor='EDEDED')
        if isinstance(v, float): cell.number_format = '0.00'
    rr += 1
for j, w in enumerate([5, 20, 6, 30, 30, 14, 12, 14] + [9] * 12 + [34], 1):
    st.column_dimensions[get_column_letter(j)].width = w
st.freeze_panes = 'C3'

# ---- sheet 3: totals
it = wb.create_sheet('Итоги')
it.cell(row=1, column=1, value='Сколько замеров совпало (считается по листу «Сводка»)').font = f_title
hdr = ['Колонка', 'Условия', 'Замеров', 'Модель совпала', 'Доля', 'Verilog есть', 'Verilog без триггера совпал', 'Доля',
       'Verilog + триггер есть', 'Verilog + триггер совпал', 'Доля']
for j, h in enumerate(hdr, 1):
    cell = it.cell(row=3, column=j, value=h); cell.font = f_head; cell.fill = fill_head; cell.border = box; cell.alignment = center
for k, c in enumerate(COLS):
    rr = 4 + k
    R = "'Сводка'!%s%d:%s%d" % (get_column_letter(colmap[(c, 'real')]), r0, get_column_letter(colmap[(c, 'real')]), last_row)
    OK = "'Сводка'!%s%d:%s%d" % (get_column_letter(colmap[(c, 'ok')]), r0, get_column_letter(colmap[(c, 'ok')]), last_row)
    S = "'Сводка'!%s%d:%s%d" % (get_column_letter(colmap[(c, 'sim')]), r0, get_column_letter(colmap[(c, 'sim')]), last_row)
    S2 = "'Сводка'!%s%d:%s%d" % (get_column_letter(colmap[(c, 'sim2')]), r0, get_column_letter(colmap[(c, 'sim2')]), last_row)
    vals = [c, CNAME[c], '=COUNT(%s)' % R, '=COUNTIF(%s,"да")' % OK, '=IF(C%d=0,"",D%d/C%d)' % (rr, rr, rr),
            '=SUMPRODUCT(ISNUMBER(%s)*ISNUMBER(%s))' % (R, S),
            '=SUMPRODUCT(ISNUMBER(%s)*ISNUMBER(%s)*(ABS(%s-%s)<0.05))' % (R, S, S, R), '=IF(F%d=0,"",G%d/F%d)' % (rr, rr, rr),
            '=SUMPRODUCT(ISNUMBER(%s)*ISNUMBER(%s))' % (R, S2),
            '=SUMPRODUCT(ISNUMBER(%s)*ISNUMBER(%s)*(ABS(%s-%s)<0.05))' % (R, S2, S2, R), '=IF(I%d=0,"",J%d/I%d)' % (rr, rr, rr)]
    for j, v in enumerate(vals, 1):
        cell = it.cell(row=rr, column=j, value=v); cell.font = f_norm; cell.border = box
        if j in (5, 8, 11): cell.number_format = '0.0%'
rr = 4 + len(COLS)
for j, v in enumerate(['Всего', '', '=SUM(C4:C%d)' % (rr - 1), '=SUM(D4:D%d)' % (rr - 1), '=IF(C%d=0,"",D%d/C%d)' % (rr, rr, rr),
                       '=SUM(F4:F%d)' % (rr - 1), '=SUM(G4:G%d)' % (rr - 1), '=IF(F%d=0,"",G%d/F%d)' % (rr, rr, rr),
                       '=SUM(I4:I%d)' % (rr - 1), '=SUM(J4:J%d)' % (rr - 1), '=IF(I%d=0,"",J%d/I%d)' % (rr, rr, rr)], 1):
    cell = it.cell(row=rr, column=j, value=v); cell.font = f_bold; cell.border = box
    if j in (5, 8, 11): cell.number_format = '0.0%'
for j, w in enumerate([9, 40, 10, 15, 9, 13, 18, 9, 16, 18, 9], 1):
    it.column_dimensions[get_column_letter(j)].width = w

# ---- sheet 4: all simulated forms
af = wb.create_sheet('Все формы')
for j, h in enumerate(['Форма', 'Циклов', 'Хвост, полутакты', 'Этапы', 'Статика 3 МГц, тактов', 'БК0010, тактов', 'БК0011М, тактов (среднее)'], 1):
    cell = af.cell(row=1, column=j, value=h); cell.font = f_head; cell.fill = fill_head; cell.border = box; cell.alignment = center
rr = 2
for key in sorted(model.table, key=str):
    t = model.table[key]
    vals = [repr(key), len(t['steps']), t['tail'], steps_text(t['steps'], t['tail']).replace('\n', '; '),
            round(model_val(key, 'D'), 2), round(model_val(key, 'B'), 2), round(model_val(key, 'E'), 2)]
    for j, v in enumerate(vals, 1):
        cell = af.cell(row=rr, column=j, value=v); cell.font = f_norm
    rr += 1
for j, w in enumerate([22, 8, 10, 110, 12, 12, 14], 1):
    af.column_dimensions[get_column_letter(j)].width = w
af.freeze_panes = 'B2'
af.auto_filter.ref = 'A1:G%d' % (rr - 1)

# ---- sheet 5: legend
lg = wb.create_sheet('Пояснения')
lines = [
    ('Что это', 'Сводка исследования времени команд К1801ВМ1 на БК (timing = vm1 в eCat3). Подробно - tools/bk-timing/README.md.'),
    ('Реал (синий шрифт)', 'Замеры с настоящих машин: таблица BK0010-11-instructions-speed.xlsx (колонки B-J), программа Manwe 45com-lo на БК0010 (real.jpg, колонка B) и на БК0011М (BK0011-45instructions.png, колонка E).'),
    ('Verilog без триггера', 'Симуляция в Icarus Verilog: синхронная модель ВМ1 (github 1801BM1/cpu11, микропрограмма А) + вентильная модель К1801ВП1-037 (github 1801BM1/k1801); RPLY контроллера идет прямо на процессор; статика - ответ на ближайшем спаде такта. 12 копий команды подряд, период по последним 8 (3 и 6 МГц) или 6 (4 МГц) - кратно периоду чередования времен.'),
    ('Verilog + триггер RPLY', 'То же, но RPLY контроллера проходит через триггер, тактируемый тактом процессора, как на схемах БК0010 (D-триггер от инверсного ТИ/4) и БК0011М (JK-триггер от такта процессора). В стенде - защелка по переднему фронту clk модели (RSYNC = 1); по заднему (RSYNC = 2) совпадений втрое меньше. В модели ВМ1 восстановлено исходное rply_ack[1] вместо rply_ack[2] - на время это не влияет. G и J - статика, триггер их не касается.'),
    ('Модель', 'Расчет эмулятора (vm1_timing.cpp, тот же расчет в tools/bk-timing/model.py): шаблон формы из симуляции fast/fast + ответ памяти.'),
    ('Параметры ВП1-037 в модели', 'окно 1333 нс; запас строба: чтение 250 нс, запись 400, запись в DATIO 700; ответ через 550 нс после точки окна. Плата статики: запись в DATIO на 2 полутакта дольше (rmw_extra = 2). Подобрано по замерам (колонки B и E, 6 МГц - проверка).'),
    ('Этапы', 'Лист «Этапы»: каждый цикл магистрали команды. «SYNC после отпускания» - промежуток, заданный микропрограммой (не зависит от памяти); строб - DIN или DOUT; RPLY - ответ; отпускание строба через 3 полутакта после RPLY. Последний этап - предвыборка кода следующей команды. Хвост - работа микропрограммы после предвыборки.'),
    ('Совпало', 'Модель и замер различаются меньше чем на 0,05 такта. Розовым отмечены несовпадения модели и ячейки Verilog, расходящиеся с замером.'),
    ('Известные расхождения', '1) Без триггера RPLY симуляция на 3 и 4 МГц на обращении к данным на окно быстрее железа. Триггер (по схеме) объясняет большую часть: совпадений на БК0010 34 -> 99, на БК0011М 32 -> 129, на 6 МГц 134 -> 175 из 192. Остаток - у части форм стенд еще на 1-3 окна быстрее; фаза тактов, несимметричный такт 11М (делитель на 3), задержки выходов, ревизия Г и rply_ack[1] его не объясняют. Поэтому параметры 037 в модели эмулятора подобраны по замерам. '
                              '2) Восемь форм ADD с приемником в памяти на БК0010: у четырех в таблице slow меньше fast - ошибки записи. '
                              '3) Колонка C (код в статике, данные в ОЗУ) у ADD в DATIO расходится на окно систематически - касается только платы статики, которой в эмуляторе нет. '
                              '4) Колонки с платой статики на 4 и 6 МГц (F, G, I, J): на 6 МГц сама плата отвечает примерно на такт позже на каждое обращение (J: +1..3 такта на команду), а модель и стенд считают ее ответ мгновенным. Для БК без платы это не имеет значения.'),
    ('Формы в «Все формы»', 'Все 1927 форм, снятых симуляцией (операция x режим источника x режим приемника, PC-режимы отдельно; ветвления, JMP/JSR, RTS, RTI, SOB, ловушки, MARK), с моделью для трех машин.'),
]
lg.column_dimensions['A'].width = 28; lg.column_dimensions['B'].width = 120
for k, (a, b) in enumerate(lines, 1):
    lg.cell(row=k, column=1, value=a).font = f_bold
    c2 = lg.cell(row=k, column=2, value=b); c2.font = f_norm; c2.alignment = wrap

wb.move_sheet('Пояснения', offset=-4)
wb.save(OUT)
print('saved', OUT, len(rows), 'rows')
