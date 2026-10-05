# Таблица времени каждой формы по вхождениям, собранным extract.py.
#   свой шаблон - форма сразу после SEC (прогоны «форма; SEC»): её шаги с
#                 промежутками, когда от предшественника ничего не осталось
#   хвост       - насколько позже начинается SEC после этой формы, чем после
#                 SEC: микропрограмма ещё работает после последнего цикла
# Хвост и первый промежуток следующей формы отсчитываются от одного момента
# (отпускания последнего строба), так что делятся они условно: у SEC хвост
# берётся таким, чтобы наименьший хвост формы был нулём.
# Проверка - повторы («форма; форма; ...»): первый промежуток = хвост + свой.
import pickle, collections, sys

occ = pickle.load(open('occ.pkl', 'rb'))
by = collections.defaultdict(collections.Counter)
for name, pred, key, steps in occ:
    by[(pred, key)][steps] += 1

CC = ('CC', 1)
def most(c): return c.most_common(1)[0][0] if c else None

g_cc_raw = most(by[(CC, CC)])[0][2]
# первый промежуток SEC после всех предшественников: наименьший - без хвоста
firsts = [most(c)[0][2] for (p, k), c in by.items() if k == CC and p is not None]
g_cc = min(firsts)
SEC_TAIL = g_cc_raw - g_cc
print('SEC после SEC: первый промежуток', g_cc_raw, '- свой у SEC', g_cc, ', хвост SEC', SEC_TAIL)

keys = sorted({k for (_, k) in by}, key=str)
table = {}
problems = []
for k in keys:
    own = most(by.get((CC, k), collections.Counter()))
    if own is None:
        continue
    sec_after = most(by.get((k, CC), collections.Counter()))
    tail = (sec_after[0][2] - g_cc) if sec_after else 0
    steps = [list(s) for s in own]
    steps[0][2] -= SEC_TAIL
    table[k] = dict(steps=steps, tail=tail)
    if len(by.get((k, CC), {})) > 1:
        problems.append(('SEC after', k, dict(by[(k, CC)])))
    if len(by.get((CC, k), {})) > 1:
        problems.append(('after SEC', k, dict(by[(CC, k)])))

# формы, не встреченные сразу после SEC: от известного предшественника
for (pred, k), cnt in list(by.items()):
    if k in table or pred not in table: continue
    steps = [list(s) for s in most(cnt)]
    steps[0][2] -= table[pred]['tail']
    tail = 0
    for (p2, k2), c in by.items():
        if p2 == k and k2 in table:
            tail = most(c)[0][2] - table[k2]['steps'][0][2]; break
    table[k] = dict(steps=steps, tail=max(0, tail))
    print('derived', k, 'from predecessor', pred, table[k])

tails = [v['tail'] for v in table.values()]
print('forms', len(table), 'tail min', min(tails), 'max', max(tails))

bad = 0
for k, v in table.items():
    rep = most(by.get((k, k), collections.Counter()))
    if rep is None: continue
    pred = [list(s) for s in v['steps']]
    pred[0][2] += v['tail']
    if [list(s) for s in rep] != pred:
        bad += 1
        if bad <= 25: print('REPEAT MISMATCH', k, 'repeat', rep, 'model', pred)
print('repeat mismatches', bad)
for p in problems[:20]: print('INCONSISTENT', p)

# вход в прерывание (часть X)
for (pred, k), c in by.items():
    if k not in (('INT',), ('VINT',)): continue
    steps = [list(s) for s in most(c)]
    steps[0][2] -= table[pred]['tail'] if pred in table else 0
    table[k] = dict(steps=steps, tail=0)
    print(k, steps, 'variants', dict(c))

# Запись: DOUT не раньше SYNC + 5 (так у всех форм, когда шина держит),
# но и не раньше, чем его выставит микропрограмма: в шаблоне после SEC (где
# шина свободна) строб бывает позже. Тогда в r2w записи - время строба от
# начала команды s0 (отпускание прошлой формы + её хвост), а strobe - 5.
# В том же прогоне по шаблону: быстрая память, отпускание чтения через 4,
# записи через 3, безадресного чтения - через r2w
W_STROBE_MIN = 5
anchored = 0
for k, v in table.items():
    t = 0
    read_before = False
    for i, st in enumerate(v['steps']):
        kind, role, gap, so, r2w = st
        sync = t + gap
        strobe = sync + so
        # после чтения данных запись ждёт их, а не начала команды
        if kind == 'W' and so > W_STROBE_MIN and not read_before:
            st[3] = W_STROBE_MIN; st[4] = strobe; anchored += 1
        if role == 'D' and kind in 'RM': read_before = True
        if role == 'N': t = strobe + r2w
        elif kind == 'R': t = strobe + 4
        elif kind == 'W': t = strobe + 3
        else: t = strobe + 4 + r2w + 3
print('записей с привязкой DOUT к началу команды', anchored)

pickle.dump(table, open('table.pkl', 'wb'))
