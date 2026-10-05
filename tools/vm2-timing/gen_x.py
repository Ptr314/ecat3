# Часть X: то, что основной генератор не раскладывает подряд, и вход в
# прерывание. JMP/JSR (R)+ с R3 по копиям выполняются дважды каждая (переход
# на себя, потом на следующую). Прерывания: MOV R0,@#маркер, по которому
# стенд даёт импульс EVNT (вектор 100) или поднимает VIRQ (вектор 340 с
# циклом IAKO); обработчик - RTI.
import json
COPIES = 8
FILL = 0o070000
mem = [FILL] * 32768
a = 0o1000
flow = []
def w(v):
    global a
    mem[a >> 1] = v & 0xFFFF; a += 2
def ins(*words):
    flow.append(a)
    for v in words: w(v)
cases = []
def preamble(r3=0):
    ins(0o005037, 0o177702)
    for r, v in {0: 0o062000, 1: 0o064000, 2: 0o066000, 4: 0o072000, 5: 0o073000}.items():
        ins(0o012700 | r, v)
    fix = a + 2
    ins(0o012703, r3)
    ins(0o012706, 0o077000)
    ins(0o106427, 0o000000)                           # MTPS #0: прерывания разрешены
    return fix
for name, word in (('JMP (R)+', 0o000123), ('JSR PC,(R)+', 0o004723)):
    start = a
    flow = []
    fix = preamble()
    copies = []
    for k in range(COPIES):
        copies.append(a); w(word)
    mem[fix >> 1] = copies[0]
    for c in copies: flow += [c, c]
    cases.append(dict(name=name, kind='twice', start=start, copies=copies, end=a, flow=list(flow)))
for name, marker, vec in (('IRQ', 0o177704, 0o100), ('VIRQ', 0o177706, 0o340)):
    start = a
    flow = []
    preamble()
    copies = []
    for k in range(COPIES):
        copies.append(a); ins(0o010037, marker)      # MOV R0,@#маркер
    end = a
    w(0o000401)                                       # BR через обработчик
    handler = a; w(0o000002)                          # RTI
    mem[vec >> 1] = handler; mem[(vec >> 1) + 1] = 0
    cases.append(dict(name=name, kind='interrupt', start=start, copies=copies, end=end,
                      handler=handler, flow=list(flow)))
for v in [0o012737, 1, 0o177700, 0o000777]:
    w(v)
mem[0] = 0o001000; mem[1] = 0
json.dump(cases, open('x0.json', 'w'))
open('x0.mem', 'w').write(''.join('%04x\n' % v for v in mem))
print(len(cases), 'cases, end', oct(a))
