# A random mix of instruction forms: the model against the simulation on code
# where every instruction follows a different one.
import random, json, sys
from gen_all import MODES, DOUBLE, SINGLE, operand, emit_operand, FILL

random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
N = 300
mem = [FILL] * 32768
a = 0o001000
def w(v):
    global a
    mem[a >> 1] = v & 0xFFFF; a += 2
w(0o005037); w(0o177702)
for r, v in {0: 0o062000, 1: 0o064000, 2: 0o066000, 3: 0, 4: 0o072000, 5: 0o073000}.items():
    w(0o012700 | r); w(v)
w(0o012706); w(0o077000)
insns = []                          # address of every timed instruction
dops = list(DOUBLE.items()); sops = list(SINGLE.items())
for i in range(N):
    insns.append(a)
    r = random.random()
    if r < 0.5:
        op, code = random.choice(dops)
        si = random.randrange(12); di = random.choice([x for x in range(12) if x != 8])
        # keep the register walks short: -(R)/(R)+ move by at most a word each
        ms, rs, ks = operand(si, 1); md, rd, kd = operand(di, 2)
        w((code << 12) | (ms << 9) | (rs << 6) | (md << 3) | rd)
        if ks: w(emit_operand(mem, a, ks, 0))
        if kd: w(emit_operand(mem, a, kd, 0))
    elif r < 0.8:
        op, code = random.choice(sops)
        di = random.choice([x for x in range(12) if x != 8])
        md, rd, kd = operand(di, 2)
        w((code << 6) | (md << 3) | rd)
        if kd: w(emit_operand(mem, a, kd, 0))
    elif r < 0.87:
        w(random.choice([0o000240, 0o000261, 0o000241]))
    elif r < 0.94:
        w(random.choice([0o000400, 0o001000, 0o001400, 0o103400, 0o103000]))
    else:
        w(0o000137); w(a + 2)       # JMP @#next
insns.append(a)
for v in [0o012737, 1, 0o177700, 0o000777]: w(v)
mem[0] = 0o000137; mem[1] = 0o001000
open('mix.mem', 'w').write(''.join('%04x\n' % v for v in mem))
json.dump(insns, open('mix.json', 'w'))
print('mix: %d instructions, code to %s' % (N, oct(a)))
