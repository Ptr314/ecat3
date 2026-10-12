# Случайный поиск + спуск по памяти ЦП (см. fit_cpu.py)
import random, fit_cpu
random.seed(1)
R = dict(grid=(80, 160, 320, 640), setup=range(0, 400, 40), reply=range(80, 960, 40),
         reply_write=range(0, 640, 40), read=range(320, 2000, 80), write=range(640, 3200, 80),
         rmw_write=range(0, 1280, 80))
res = []
for i in range(600):
    kw = {k: random.choice(list(v)) for k, v in R.items()}
    res.append((fit_cpu.evaluate(kw), kw))
res.sort(key=lambda x: x[0])
for e, kw in res[:8]: print('%.3f' % e, kw, flush=True)
