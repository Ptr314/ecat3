// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Disassembler widget's header

#pragma once

#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/disasm.h"
#include "dosframe.h"

#define DISASM_SIZE 1000

struct DisAsmEntry {
    unsigned int address;
    unsigned int len;
    QString code;
    QString command;
    bool breakpoint;
    bool current;
};

class DisAsmArea : public DOSFrame
{
    Q_OBJECT
public:
    explicit DisAsmArea(QWidget *parent = nullptr);

    void set_data(Emulator * e, CPU * cpu, DisAsm * disasm, unsigned int address);
    void go_to(unsigned int address);
    unsigned int get_address_at_cursor();
    void invalidate();
    void keyPressEvent(QKeyEvent *event) override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    Emulator * e;
    CPU * cpu;
    DisAsm * disasm;
    uint16_t CRC;

    unsigned int font_height;
    unsigned int char_width;
    unsigned int address_first;
    unsigned int address;
    unsigned int address_last;
    int lines_count;
    DisAsmEntry lines[DISASM_SIZE];
    int max_lines;
    int screen_size;
    int first_line;
    int cursor_line;
    bool data_valid;

    void update_data();

    void move_cursor(int increment);

    uint16_t area_crc16();
    void disassemble_lines(int index, int count, unsigned int address=0);

signals:
    void command_key(QKeyEvent *event);
};
