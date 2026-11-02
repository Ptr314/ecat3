// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8051 disassembler

#include <cstdio>

#include "disasm_i8051.h"

// Операнды шаблона: d - байт прямого адреса, b - адрес бита, i - непосредственный
// байт, r - относительный переход, a - адрес из двух байтов, s - адрес 11 разрядов
// команд AJMP и ACALL, j - 16-разрядный непосредственный
static const char * const TEMPLATES[256] = {
    "NOP", "AJMP s", "LJMP a", "RR A", "INC A", "INC d", "INC @R0", "INC @R1",
    "INC R0", "INC R1", "INC R2", "INC R3", "INC R4", "INC R5", "INC R6", "INC R7",
    "JBC b,r", "ACALL s", "LCALL a", "RRC A", "DEC A", "DEC d", "DEC @R0", "DEC @R1",
    "DEC R0", "DEC R1", "DEC R2", "DEC R3", "DEC R4", "DEC R5", "DEC R6", "DEC R7",
    "JB b,r", "AJMP s", "RET", "RL A", "ADD A,#i", "ADD A,d", "ADD A,@R0", "ADD A,@R1",
    "ADD A,R0", "ADD A,R1", "ADD A,R2", "ADD A,R3", "ADD A,R4", "ADD A,R5", "ADD A,R6", "ADD A,R7",
    "JNB b,r", "ACALL s", "RETI", "RLC A", "ADDC A,#i", "ADDC A,d", "ADDC A,@R0", "ADDC A,@R1",
    "ADDC A,R0", "ADDC A,R1", "ADDC A,R2", "ADDC A,R3", "ADDC A,R4", "ADDC A,R5", "ADDC A,R6", "ADDC A,R7",
    "JC r", "AJMP s", "ORL d,A", "ORL d,#i", "ORL A,#i", "ORL A,d", "ORL A,@R0", "ORL A,@R1",
    "ORL A,R0", "ORL A,R1", "ORL A,R2", "ORL A,R3", "ORL A,R4", "ORL A,R5", "ORL A,R6", "ORL A,R7",
    "JNC r", "ACALL s", "ANL d,A", "ANL d,#i", "ANL A,#i", "ANL A,d", "ANL A,@R0", "ANL A,@R1",
    "ANL A,R0", "ANL A,R1", "ANL A,R2", "ANL A,R3", "ANL A,R4", "ANL A,R5", "ANL A,R6", "ANL A,R7",
    "JZ r", "AJMP s", "XRL d,A", "XRL d,#i", "XRL A,#i", "XRL A,d", "XRL A,@R0", "XRL A,@R1",
    "XRL A,R0", "XRL A,R1", "XRL A,R2", "XRL A,R3", "XRL A,R4", "XRL A,R5", "XRL A,R6", "XRL A,R7",
    "JNZ r", "ACALL s", "ORL C,b", "JMP @A+DPTR", "MOV A,#i", "MOV d,#i", "MOV @R0,#i", "MOV @R1,#i",
    "MOV R0,#i", "MOV R1,#i", "MOV R2,#i", "MOV R3,#i", "MOV R4,#i", "MOV R5,#i", "MOV R6,#i", "MOV R7,#i",
    "SJMP r", "AJMP s", "ANL C,b", "MOVC A,@A+PC", "DIV AB", "MOV D,d", "MOV d,@R0", "MOV d,@R1",
    "MOV d,R0", "MOV d,R1", "MOV d,R2", "MOV d,R3", "MOV d,R4", "MOV d,R5", "MOV d,R6", "MOV d,R7",
    "MOV DPTR,#j", "ACALL s", "MOV b,C", "MOVC A,@A+DPTR", "SUBB A,#i", "SUBB A,d", "SUBB A,@R0", "SUBB A,@R1",
    "SUBB A,R0", "SUBB A,R1", "SUBB A,R2", "SUBB A,R3", "SUBB A,R4", "SUBB A,R5", "SUBB A,R6", "SUBB A,R7",
    "ORL C,/b", "AJMP s", "MOV C,b", "INC DPTR", "MUL AB", nullptr, "MOV @R0,d", "MOV @R1,d",
    "MOV R0,d", "MOV R1,d", "MOV R2,d", "MOV R3,d", "MOV R4,d", "MOV R5,d", "MOV R6,d", "MOV R7,d",
    "ANL C,/b", "ACALL s", "CPL b", "CPL C", "CJNE A,#i,r", "CJNE A,d,r", "CJNE @R0,#i,r", "CJNE @R1,#i,r",
    "CJNE R0,#i,r", "CJNE R1,#i,r", "CJNE R2,#i,r", "CJNE R3,#i,r", "CJNE R4,#i,r", "CJNE R5,#i,r", "CJNE R6,#i,r", "CJNE R7,#i,r",
    "PUSH d", "AJMP s", "CLR b", "CLR C", "SWAP A", "XCH A,d", "XCH A,@R0", "XCH A,@R1",
    "XCH A,R0", "XCH A,R1", "XCH A,R2", "XCH A,R3", "XCH A,R4", "XCH A,R5", "XCH A,R6", "XCH A,R7",
    "POP d", "ACALL s", "SETB b", "SETB C", "DA A", "DJNZ d,r", "XCHD A,@R0", "XCHD A,@R1",
    "DJNZ R0,r", "DJNZ R1,r", "DJNZ R2,r", "DJNZ R3,r", "DJNZ R4,r", "DJNZ R5,r", "DJNZ R6,r", "DJNZ R7,r",
    "MOVX A,@DPTR", "AJMP s", "MOVX A,@R0", "MOVX A,@R1", "CLR A", "MOV A,d", "MOV A,@R0", "MOV A,@R1",
    "MOV A,R0", "MOV A,R1", "MOV A,R2", "MOV A,R3", "MOV A,R4", "MOV A,R5", "MOV A,R6", "MOV A,R7",
    "MOVX @DPTR,A", "ACALL s", "MOVX @R0,A", "MOVX @R1,A", "CPL A", "MOV d,A", "MOV @R0,A", "MOV @R1,A",
    "MOV R0,A", "MOV R1,A", "MOV R2,A", "MOV R3,A", "MOV R4,A", "MOV R5,A", "MOV R6,A", "MOV R7,A"
};

static const char * sfr_name(unsigned int address)
{
    switch (address) {
    case 0x80: return "P0";
    case 0x81: return "SP";
    case 0x82: return "DPL";
    case 0x83: return "DPH";
    case 0x87: return "PCON";
    case 0x88: return "TCON";
    case 0x89: return "TMOD";
    case 0x8A: return "TL0";
    case 0x8B: return "TL1";
    case 0x8C: return "TH0";
    case 0x8D: return "TH1";
    case 0x90: return "P1";
    case 0x98: return "SCON";
    case 0x99: return "SBUF";
    case 0xA0: return "P2";
    case 0xA8: return "IE";
    case 0xB0: return "P3";
    case 0xB8: return "IP";
    case 0xD0: return "PSW";
    case 0xE0: return "ACC";
    case 0xF0: return "B";
    default:   return nullptr;
    }
}

static std::string hex(unsigned int value, int width)
{
    char s[12];
    snprintf(s, sizeof(s), "%0*X", width, value);
    return s;
}

static std::string direct_text(unsigned int address)
{
    const char * name = sfr_name(address);
    return name != nullptr ? std::string(name) : "$" + hex(address, 2);
}

static std::string bit_text(unsigned int bit)
{
    if (bit < 0x80) return "$" + hex(0x20 + (bit >> 3), 2) + "." + std::to_string(bit & 7);
    return direct_text(bit & 0xF8) + "." + std::to_string(bit & 7);
}

DisAsmI8051::DisAsmI8051()
{
    max_command_length = 3;
}

emulator::Result DisAsmI8051::load_file(const std::string &)
{
    return emulator::Result::ok();
}

unsigned int DisAsmI8051::decode(CommandBytes bytes, unsigned int PC, unsigned int, DisasmLine * out)
{
    const uint8_t * b = *bytes;
    const unsigned int op = b[0];
    const char * t = TEMPLATES[op];
    unsigned int length = 1;
    std::string text;
    bool target = false;
    unsigned int to = 0;

    if (t == nullptr) {
        out->length = 1;
        out->text = "DB $" + hex(op, 2);
        out->valid = false;
        return 1;
    }

    // Операнды идут в байтах по порядку шаблона; только MOV d,d хранит
    // источник первым, и шаблон пишет его заглавной буквой
    for (const char * p = t; *p != 0; p++) {
        const bool word_start = p == t || p[-1] == ' ' || p[-1] == ',' || p[-1] == '#' || p[-1] == '/';
        const bool word_end = p[1] == 0 || p[1] == ',';
        if (!word_start || !word_end) {
            text += *p;
            continue;
        }
        switch (*p) {
        case 'd': text += direct_text(b[length++]); break;
        case 'D': text += direct_text(b[length + 1]); text += ","; text += direct_text(b[length]); length += 2; p += 2; break;
        case 'b': text += bit_text(b[length++]); break;
        case 'i': text += "$" + hex(b[length++], 2); break;
        case 'r':
            to = (PC + length + 1 + (int8_t)b[length]) & 0xFFFF;
            length++;
            target = true;
            text += "$" + hex(to, 4);
            break;
        case 'a':
            to = ((unsigned int)b[length] << 8) | b[length + 1];
            length += 2;
            target = true;
            text += "$" + hex(to, 4);
            break;
        case 'j':
            text += "$" + hex(((unsigned int)b[length] << 8) | b[length + 1], 4);
            length += 2;
            break;
        case 's':
            to = ((PC + 2) & 0xF800) | ((op & 0xE0u) << 3) | b[length];
            length++;
            target = true;
            text += "$" + hex(to, 4);
            break;
        default:
            text += *p;
            break;
        }
    }

    out->length = length;
    out->text = text;
    out->valid = true;
    if (target) {
        out->has_target = true;
        out->target = to;
    }
    const std::string m(t);
    if (m == "RET" || m == "RETI") {
        out->flow = FLOW_RET;
    } else if (m.compare(0, 5, "LCALL") == 0 || m.compare(0, 5, "ACALL") == 0) {
        out->flow = FLOW_CALL;
    } else if (m.compare(0, 4, "LJMP") == 0 || m.compare(0, 4, "AJMP") == 0 || m.compare(0, 4, "SJMP") == 0) {
        out->flow = FLOW_JUMP;
    } else if (m.compare(0, 3, "JMP") == 0) {
        out->flow = FLOW_JUMP;
    } else if (target) {
        out->flow = FLOW_JUMP;
        out->conditional = true;
    }
    return length;
}
