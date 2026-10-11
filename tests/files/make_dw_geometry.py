#!/usr/bin/env python3
# Образ винчестера DW для work-dvk4-dw1: 2 цилиндра, 8 головок, 8 секторов
# (64 КБ). В начале каждого сектора - номер блока, цилиндр, головка и сектор
# (с единицы), как его пишет программа; остальное - нули.
import os
import struct

CYLINDERS, HEADS, SECTORS = 2, 8, 8

out = bytearray()
for cyl in range(CYLINDERS):
    for head in range(HEADS):
        for sector in range(SECTORS):
            block = (cyl * HEADS + head) * SECTORS + sector
            out += struct.pack('<4H', block, cyl, head, sector + 1).ljust(512, b'\0')

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dw-8heads.img'), 'wb') as f:
    f.write(out)
