import sys, patron, emu_model as em
def run(kw, verbose=False):
    tot = 0; n = 0; rows = []
    for name, w, real in patron.forms():
        v = em.period(w, em.Mem(125000, drift=100, **kw), 200, 3200)
        tot += abs(v - real); n += 1; rows.append(v)
    if verbose:
        for i in range(9): print(' '.join('%5.1f' % rows[i * 9 + j] for j in range(9)))
    return tot / n
A = dict(grid=320, setup=0, reply=480, reply_write=320, read=960, write=2240, rmw_write=640)
B = dict(A, reply=520)
print('current mae %.2f' % run(A, True)); print('reply 520 mae %.2f' % run(B))
