#!/usr/bin/env python3
# Makes poisk-test.cas, a test cassette for the Поиск-1 in the CAS format (the
# bytes the IBM PC cassette BIOS writes, see src/emulator/devices/common/tape_ibmpc.h).
#
# Two records, as FILE_WRITE of the BIOS leaves them: a header of 256 bytes
# (A5h, the name test padded with spaces, type 01, the length, segment 0060h,
# offset 0) and the data - a program that prints "TAPE LOAD OK" through INT 10h
# and stops. The menu of the BIOS (F1, the name, Enter) loads it to 0060:0000
# and runs it.

import os
import struct

def crc16(data):
    crc = 0xFFFF
    for byte in data:
        for k in range(7, -1, -1):
            fb = ((crc >> 15) & 1) ^ ((byte >> k) & 1)
            crc = (crc << 1) & 0xFFFF
            if fb:
                crc ^= 0x1021
    return crc

def record(data):
    # The BIOS fills the last block with the byte that follows the data in
    # memory, again and again; here it is 0
    out = bytearray(b'\xFF' * 256 + b'\xFE' + b'\x16')
    pos = 0
    while True:
        block = bytearray(data[pos:pos + 256])
        block += bytes(256 - len(block))
        out += block
        out += struct.pack('>H', crc16(block) ^ 0xFFFF)
        pos += 256
        if pos >= len(data):
            break
    out += b'\xFF' * 4
    return out

msg = b'TAPE LOAD OK\r\n\0'
program = bytes.fromhex(
    '0E'            # push cs
    '1F'            # pop ds
    'BE1500'        # mov si,msg
    'AC'            # L: lodsb
    '08C0'          # or al,al
    '7409'          # jz done
    'B40E'          # mov ah,0Eh
    'BB0700'        # mov bx,7
    'CD10'          # int 10h
    'EBF2'          # jmp L
    'EBFE'          # done: jmp $
) + msg
assert len(program) == 0x15 + len(msg)

header = bytearray(17)
header[0] = 0xA5
header[1:9] = b'test    '
header[9] = 0x01
header[10:12] = struct.pack('<H', len(program))
header[12:14] = struct.pack('<H', 0x0060)
header[14:16] = struct.pack('<H', 0)

cas = record(bytes(header)) + record(program)
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'poisk-test.cas')
with open(path, 'wb') as f:
    f.write(cas)
print(path, len(cas), 'bytes')

# The same program as a bare file, the way digitised cassettes are often kept:
# no leader, no header, no CRC. The tape recorder makes the records itself and
# names the header after the file, so the menu wants the name pbare
bare = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pbare.cas')
with open(bare, 'wb') as f:
    f.write(program)
print(bare, len(program), 'bytes')
