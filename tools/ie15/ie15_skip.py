"""Where in the 15ИЭ firmware a skipping instruction is followed by RAL/RAR (033/035).

The emulator (IE15CPU::skip()) and MAME skip a whole 2-byte slot when the skipped
opcode is in group 03x, so a skipped RAL/RAR would swallow the byte after it as
well; the ТО does not say what the hardware does. This finds whether the firmware
ever puts RAL/RAR right after an instruction that can skip: if it never does, the
question does not matter.

The code is followed from address 0: a JMP goes to its target, a skipping
instruction takes both ways, and since JMI jumps through a register, every LDC
constant is taken as an entry point too (so some data is walked as code: an
unknown 03x opcode reported there is most likely a byte of a table). Then every
address of the ROM is checked as well, whether reached or not.

Usage: python ie15_skip.py [ROM...]   (default: both firmwares in deploy)
"""
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DEFAULT_ROMS = [os.path.join(ROOT, 'deploy', 'computers', 'dvk', 'files', n)
                for n in ('ie15.rom', 'ie15_6chip.rom')]


def length(op):
    return 2 if (op & 0xF0) in (0x10, 0x20) or op == 0x30 else 1


def can_skip(op):
    hi = op & 0xF0
    if hi in (0x40, 0xB0):                  # DSR, CS
        return True
    if op in (0x50, 0x52, 0x58, 0x5A):      # ISN, DSN, ISE, DSE
        return True
    return hi == 0x80 and op != 0x8F        # SFC/SFS/SKP; NOP never skips


def skip_len(op):
    """What IE15CPU::skip() steps over after a skipping instruction."""
    return 2 if (op & 0xF0) in (0x10, 0x20, 0x30) else 1


def name(op):
    hi = op & 0xF0
    if hi == 0x80:
        return 'skp' if op == 0x87 else ('sfs ' if op & 8 else 'sfc ') + str(op & 7)
    if hi == 0x50:
        return {0x50: 'isn', 0x52: 'dsn', 0x58: 'ise', 0x5A: 'dse'}[op]
    return ('dsr' if hi == 0x40 else 'cs') + f' r{op & 15}'


def reachable(rom):
    seen = set()
    work = [0]
    for pc in range(len(rom) - 1):
        if rom[pc] & 0xF0 == 0x20:          # LDC: the page is that of the code
            work.append((rom[pc + 1] | ((pc + 2) & 0xF00)) & 0xFFF)
    while work:
        pc = work.pop()
        while pc not in seen:
            seen.add(pc)
            op = rom[pc]
            hi = op & 0xF0
            if hi == 0x10:                  # JMP: page from the code, address + 1
                work.append(((((op & 15) << 8) | rom[(pc + 1) & 0xFFF]) + 1) & 0xFFF)
                break
            if hi == 0x70:                  # JMI: the target is not known here
                break
            if can_skip(op):
                nxt = (pc + 1) & 0xFFF
                work.append((nxt + skip_len(rom[nxt])) & 0xFFF)
            pc = (pc + length(op)) & 0xFFF
    return seen


def scan(path):
    rom = open(path, 'rb').read()
    code = reachable(rom)
    print(f'== {path}: {len(rom)} bytes, {len(code)} reached')
    found = 0
    for pc in range(len(rom) - 1):
        op, nxt = rom[pc], rom[pc + 1]
        if can_skip(op) and nxt in (0x33, 0x35):
            found += 1
            where = 'code' if pc in code else 'not reached'
            print(f'  {pc:04o}: {name(op)} then {"ral" if nxt == 0x33 else "rar"} ({where}),'
                  f' a skip would also take {rom[(pc + 2) & 0xFFF]:03o}')
    for pc in sorted(code):
        if rom[pc] & 0xF0 == 0x30 and rom[pc] not in (0x30, 0x33, 0x35):
            print(f'  unknown 03x opcode {rom[pc]:03o} at {pc:04o} (reached)')
    ral = sum(1 for pc in code if rom[pc] in (0x33, 0x35))
    print(f'  RAL/RAR reached: {ral}; right after a skipping instruction, anywhere: {found}')
    return found


if __name__ == '__main__':
    total = sum(scan(p) for p in (sys.argv[1:] or DEFAULT_ROMS))
    sys.exit(1 if total else 0)
