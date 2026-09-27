# Part X: forms the main generator cannot lay out, and the interrupt entry
import json
COPIES = 8
FILL = 0o070000
mem = [FILL] * 32768
a = 0o1000
def w(v):
    global a
    mem[a >> 1] = v & 0xFFFF; a += 2
cases = []
def preamble(r3=0):
    w(0o005037); w(0o177702)                          # restore data
    for r, v in {0: 0o062000, 1: 0o064000, 2: 0o066000, 4: 0o072000, 5: 0o073000}.items():
        w(0o012700 | r); w(v)
    w(0o012703); fix = a; w(r3)
    w(0o012706); w(0o077000)
    w(0o106427); w(0o000000)                          # MTPS #0: interrupts enabled
    return fix
for name, word in (('JMP (R)+', 0o000123), ('JSR PC,(R)+', 0o004723)):
    start = a
    fix = preamble()
    copies = []
    for k in range(COPIES):
        copies.append(a); w(word)
    mem[fix >> 1] = copies[0]
    cases.append(dict(name=name, kind='twice', start=start, copies=copies, end=a))
handlers = {}
for name, marker, vec in (('IRQ2', 0o177704, 0o100), ('VIRQ', 0o177706, 0o340)):
    start = a
    preamble()
    copies = []
    for k in range(COPIES):
        copies.append(a); w(0o010037); w(marker)      # MOV R0,@#marker
    end = a
    w(0o000401)                                       # BR over the handler
    handler = a; w(0o000002)                          # RTI
    mem[vec >> 1] = handler; mem[(vec >> 1) + 1] = 0
    cases.append(dict(name=name, kind='interrupt', start=start, copies=copies, end=end, handler=handler))
for v in [0o012737, 1, 0o177700, 0o000777]:
    w(v)
mem[0] = 0o000137; mem[1] = 0o001000
json.dump(cases, open('x0.json', 'w'))
open('x0.mem', 'w').write(''.join('%04x\n' % v for v in mem))
print(len(cases), 'cases, end', oct(a))
