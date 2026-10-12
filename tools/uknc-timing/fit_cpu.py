# Подбор памяти ЦП покоординатным спуском на копии расчёта эмулятора:
# формы TSSPD (без PC и кэша) и BR, BCS без перехода, SEC, SOB из OP1 и JSR.
#   fit_cpu.py [ключ=значение ...]   - начальная точка
import sys, tests, emu_model as em
EXCL = ('(PC)', 'miss', 'hit', 'MUL', 'JMP')
FORMS = [(n, t['words'](0)[0], True, c) for n, (t, c, p) in tests.TSSPD.items() if not any(e in n for e in EXCL)]
FORMS += [('BR', 0o000400, True, 25.0), ('BCS not', 0o103400, False, 13.0), ('SEC', 0o000261, True, 14.0),
          ('SOB R3,.', 0o077301, True, 30.7)]
import patron
FORMS += [(n, w, True, float(r)) for n, w, r in patron.forms()]
STEPS = dict(grid=(80, 160, 320, 640), setup=40, reply=40, reply_write=40, read=80, write=80, rmw_write=80)
# Регистровые команды в программах самые частые, и SPEED на машинах даёт
# сложение регистр-регистр 624-672 тысячи в секунду - 11.9-12.8 такта. Это
# форма MOV R0,R0 (TSSPD: 12.32); вне этих пределов точка не годится, какой
# бы хорошей ни была средняя по формам с памятью
REG_RANGE = (11.9, 12.8)
def evaluate(kw, verbose=False):
    tot = 0; penalty = 0
    for n, w, taken, real in FORMS:
        v = em.period(w, em.Mem(125000, drift=100, **kw), 200, 3200, taken=taken)
        tot += abs(v - real)
        if n == 'MOV R0,R0' and not REG_RANGE[0] <= v <= REG_RANGE[1]:
            penalty = 10 + abs(v - sum(REG_RANGE) / 2)
        if verbose: print('  %-22s %6.2f real %6.2f %+6.2f' % (n, v, real, v - real))
    return tot / len(FORMS) + penalty
kw = dict(grid=320, setup=0, reply=480, reply_write=320, read=960, write=2240, rmw_write=640)
for a in sys.argv[1:]:
    k, v = a.split('='); kw[k] = int(v)
if __name__ == "__main__":
  best = evaluate(kw); print('start', kw, '%.3f' % best, flush=True)
  improved = True
  while improved:
      improved = False
      for k, st in STEPS.items():
          cands = [g for g in st if g != kw[k]] if isinstance(st, tuple) else [kw[k] - st, kw[k] + st]
          for c in cands:
              if c < 0: continue
              t = dict(kw); t[k] = c
              e = evaluate(t)
              if e < best - 1e-4:
                  best, kw, improved = e, t, True
                  print('  ', k, c, '%.3f' % best, flush=True)
  print('best', kw, '%.3f' % best)
  evaluate(kw, True)
