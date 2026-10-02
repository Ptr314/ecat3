#!/usr/bin/env python3
# Makes poisk-boot.img, a test boot diskette for the Поиск-1 (B504 cartridge,
# 80 cylinders, 2 sides, 9 sectors of 512 bytes, sectors dumped cylinder by
# cylinder). Only the sectors used are written: the drive reads a shorter
# image as a diskette with an empty rest.
#
# The boot sector (loaded at 0000:7C00) prints a line through INT 10h, reads
# cylinder 1, head 1, sector 3 through INT 13h into 0000:8000 - a seek and the
# second side - prints the text found there, writes that sector to cylinder
# 2, head 0, sector 5, reads it back into 0000:9000, prints it again and
# stops. A failed read or write prints "READ ERROR" and the code in AH.

import os

ORG = 0x7C00

prog = []
def h(s): prog.append(bytes.fromhex(s))
def L(name): prog.append(name)
def w(pre, label, post=""):
    prog.append(lambda lab, off: bytes.fromhex(pre) + (lab[label]).to_bytes(2, "little") + bytes.fromhex(post))
def rel8(op, label):
    prog.append(lambda lab, off: bytes([op, (lab[label] - (off + 2)) & 0xFF]))
def rel16(op, label):
    prog.append(lambda lab, off: bytes([op]) + ((lab[label] - (off + 3)) & 0xFFFF).to_bytes(2, "little"))
def text(s): prog.append(s.encode("ascii") + b"\0")

h("FA 31C0 8ED8 8EC0 8ED0 BC007C FB")   # cli; ds = es = ss = 0; sp = 7C00; sti
w("BE", "hello")                        # mov si,hello
rel16(0xE8, "print")                    # call print
h("B80102 BB0080 B90301 BA0001")        # ax = 0201, bx = 8000, cx = 0103, dx = 0100
h("CD13")                               # int 13h
rel8(0x72, "fail")                      # jc fail
h("BE0080")                             # mov si,8000h
rel16(0xE8, "print")
h("B80103 BB0080 B90502 BA0000")        # write it to cylinder 2, head 0, sector 5
h("CD13")
rel8(0x72, "fail")
h("B80102 BB0090 B90502 BA0000")        # and read it back into 0000:9000
h("CD13")
rel8(0x72, "fail")
w("BE", "again")
rel16(0xE8, "print")
h("BE0090")                             # mov si,9000h
rel16(0xE8, "print")
L("hang")
rel8(0xEB, "hang")                      # jmp $
L("fail")
h("88E0 0430")                          # mov al,ah; add al,'0' (low digit is enough here)
h("A2")                                 # mov [code],al
prog.append(lambda lab, off: (lab["code"]).to_bytes(2, "little"))
w("BE", "error")
rel16(0xE8, "print")
rel8(0xEB, "hang")
L("print")
h("AC 08C0")                            # lodsb; or al,al
rel8(0x74, "done")                      # jz done
h("B40E BB0700 CD10")                   # mov ah,0Eh; mov bx,7; int 10h
rel8(0xEB, "print")
L("done")
h("C3")
L("hello")
text("POISK-1 BOOT SECTOR\r\n")
L("again")
text("WRITTEN TO CYLINDER 2 SECTOR 5 AND READ BACK:\r\n")
L("error")
prog.append(b"READ ERROR ")
L("code")
text("?\r\n")

def assemble(lab):
    out = b""
    for it in prog:
        if isinstance(it, str):
            lab[it] = ORG + len(out)
        elif callable(it):
            out += it(lab, ORG + len(out))
        else:
            out += it
    return out

#Two passes: the sizes do not depend on the label values, so the first pass
#(with every label at ORG) finds where they are and the second uses them
labels = {}
for it in prog:
    if isinstance(it, str): labels[it] = ORG
assemble(labels)
code = assemble(labels)

boot = bytearray(512)
boot[:len(code)] = code
boot[510:512] = b"\x55\xAA"

def offset(cyl, head, sector):
    return ((cyl * 2 + head) * 9 + (sector - 1)) * 512

data = bytearray(offset(1, 1, 3) + 512)
data[0:512] = boot
msg = b"CYLINDER 1 HEAD 1 SECTOR 3 READ OK\r\n\0"
data[offset(1, 1, 3):offset(1, 1, 3) + len(msg)] = msg

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "poisk-boot.img")
with open(path, "wb") as f:
    f.write(data)
print(path, len(data), "bytes, boot code", len(code), "bytes")
