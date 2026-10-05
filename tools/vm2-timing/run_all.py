# Все программы форм (all*, pair*, x0) на модели, параллельно, с трассой:
# log_<имя>.txt в текущем каталоге. WINDOW=1 в окружении - с видеопамятью
# КЦГД (для проверки модели), тогда логи - wlog_<имя>.txt.
import sys, os, glob, concurrent.futures
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm2sim

window = int(os.environ.get('WINDOW', '0'))
trace = int(os.environ.get('TRACE', '1'))
names = sys.argv[1:] or [m[:-4] for m in sorted(glob.glob('all*.mem') + glob.glob('pair*.mem') + glob.glob('x*.mem'))]
vvp = vm2sim.build(window, trace)
prefix = {0: 'log_', 1: 'wlog_', 2: 'dlog_', 3: 'clog_'}[window]
with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
    list(ex.map(lambda n: vm2sim.run(vvp, n + '.mem', prefix + n + '.txt'), names))
for n in names:
    print(n, vm2sim.end_clock(prefix + n + '.txt'))
