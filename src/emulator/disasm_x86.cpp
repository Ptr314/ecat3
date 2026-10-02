// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8086/8088 disassembler

#include "disasm_x86.h"
#include "emulator/utils.h"

namespace {

const char * REG8[8]  = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
const char * REG16[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
const char * SREG[4]  = {"es", "cs", "ss", "ds"};
const char * EA[8]    = {"bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bp", "bx"};
const char * ALU[8]   = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
const char * SHIFT[8] = {"rol", "ror", "rcl", "rcr", "shl", "shr", "setmo", "sar"};
const char * GRP3[8]  = {"test", "test", "not", "neg", "mul", "imul", "div", "idiv"};
const char * GRP5[8]  = {"inc", "dec", "call", "call far", "jmp", "jmp far", "push", "push"};
const char * JCC[16]  = {"jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "ja",
                         "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"};

std::string h2(unsigned v) { return "$" + hex_str(v & 0xFF, 2); }
std::string h4(unsigned v) { return "$" + hex_str(v & 0xFFFF, 4); }
std::string h5(unsigned v) { return "$" + hex_str(v & 0xFFFFF, 5); }

struct Reader {
    CommandBytes bytes;
    unsigned int pos;
    unsigned int max;
    bool short_of = false;

    uint8_t b()
    {
        if (pos >= max || pos >= 15) { short_of = true; return 0; }
        return (*bytes)[pos++];
    }
    uint16_t w()
    {
        const uint8_t lo = b();
        return (uint16_t)(lo | (b() << 8));
    }
};

struct ModRM {
    unsigned mod, reg, rm;
    std::string mem;    //the memory operand, segment included
};

ModRM modrm(Reader &r, const std::string &seg)
{
    ModRM m;
    const uint8_t v = r.b();
    m.mod = v >> 6;
    m.reg = (v >> 3) & 7;
    m.rm = v & 7;
    if (m.mod == 3) return m;
    std::string s;
    if (m.mod == 0 && m.rm == 6) {
        s = h4(r.w());
    } else {
        s = EA[m.rm];
        if (m.mod == 1) {
            const int8_t d = (int8_t)r.b();
            s += (d < 0) ? "-" + h2((unsigned)(-d)) : "+" + h2((unsigned)d);
        } else if (m.mod == 2) {
            s += "+" + h4(r.w());
        }
    }
    m.mem = (seg.empty() ? "" : seg + ":") + "[" + s + "]";
    return m;
}

std::string rm8(const ModRM &m) { return (m.mod == 3) ? REG8[m.rm] : "byte " + m.mem; }
std::string rm16(const ModRM &m) { return (m.mod == 3) ? REG16[m.rm] : "word " + m.mem; }
//Where the size is told by the other operand
std::string rm8s(const ModRM &m) { return (m.mod == 3) ? REG8[m.rm] : m.mem; }
std::string rm16s(const ModRM &m) { return (m.mod == 3) ? REG16[m.rm] : m.mem; }

} // namespace

DisAsmX86::DisAsmX86(const uint16_t * cs):
    m_cs(cs)
{
    //Prefixes, opcode, ModR/M, a displacement and an immediate word: LOCK,
    //REP and a segment in front of MOV [bx+disp16], imm16 make 9
    max_command_length = 10;
}

emulator::Result DisAsmX86::load_file(MAYBE_UNUSED const std::string &file_name)
{
    return emulator::Result::ok();
}

unsigned int DisAsmX86::disassemle(CommandBytes bytes, unsigned int PC, unsigned int max_len, std::string * output)
{
    Reader r;
    r.bytes = bytes;
    r.pos = 0;
    r.max = max_len;

    std::string prefix, seg;
    uint8_t op;
    for (;;) {
        op = r.b();
        if (r.short_of) break;
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) seg = SREG[(op >> 3) & 3];
        else if (op == 0xF0 || op == 0xF1) prefix += "lock ";
        else if (op == 0xF2) prefix += "repne ";
        else if (op == 0xF3) prefix += "rep ";
        else break;
    }

    const std::string sseg = seg.empty() ? "" : seg + ":";
    std::string s;
    //IP wraps inside the segment: a short jump back from IP 0002 lands at
    //CS:FFxx, not below the segment
    const unsigned base = (m_cs != nullptr) ? ((unsigned)*m_cs << 4) : 0;
    const bool in_cs = m_cs != nullptr && PC >= base && PC - base <= 0xFFFF;
    auto target = [&](int d) -> unsigned {
        if (!in_cs) return (unsigned)(PC + r.pos + d) & 0xFFFFF;
        return (base + ((PC - base + r.pos + d) & 0xFFFF)) & 0xFFFFF;
    };
    auto rel8 = [&]() -> std::string { const int8_t d = (int8_t)r.b(); return h5(target(d)); };
    auto rel16 = [&]() -> std::string { const int16_t d = (int16_t)r.w(); return h5(target(d)); };

    if (op < 0x40 && (op & 7) < 6) {
        const char * name = ALU[op >> 3];
        switch (op & 7) {
        case 0: { ModRM m = modrm(r, seg); s = std::string(name) + " " + rm8s(m) + ", " + REG8[m.reg]; break; }
        case 1: { ModRM m = modrm(r, seg); s = std::string(name) + " " + rm16s(m) + ", " + REG16[m.reg]; break; }
        case 2: { ModRM m = modrm(r, seg); s = std::string(name) + " " + REG8[m.reg] + ", " + rm8s(m); break; }
        case 3: { ModRM m = modrm(r, seg); s = std::string(name) + " " + REG16[m.reg] + ", " + rm16s(m); break; }
        case 4: s = std::string(name) + " al, " + h2(r.b()); break;
        default: s = std::string(name) + " ax, " + h4(r.w()); break;
        }
    } else if (op < 0x40) {
        switch (op) {
        case 0x06: case 0x0E: case 0x16: case 0x1E: s = std::string("push ") + SREG[(op >> 3) & 3]; break;
        case 0x07: case 0x0F: case 0x17: case 0x1F: s = std::string("pop ") + SREG[(op >> 3) & 3]; break;
        case 0x27: s = "daa"; break;
        case 0x2F: s = "das"; break;
        case 0x37: s = "aaa"; break;
        case 0x3F: s = "aas"; break;
        default: s = "db " + h2(op); break;   //a prefix with nothing after it
        }
    } else if (op < 0x48) s = std::string("inc ") + REG16[op & 7];
    else if (op < 0x50) s = std::string("dec ") + REG16[op & 7];
    else if (op < 0x58) s = std::string("push ") + REG16[op & 7];
    else if (op < 0x60) s = std::string("pop ") + REG16[op & 7];
    else if (op < 0x80) s = std::string(JCC[op & 0x0F]) + " " + rel8();
    else if (op >= 0x90 && op < 0x98) s = (op == 0x90) ? "nop" : std::string("xchg ax, ") + REG16[op & 7];
    else if (op >= 0xB0 && op < 0xB8) s = std::string("mov ") + REG8[op & 7] + ", " + h2(r.b());
    else if (op >= 0xB8 && op < 0xC0) s = std::string("mov ") + REG16[op & 7] + ", " + h4(r.w());
    else if (op >= 0xD8 && op < 0xE0) { ModRM m = modrm(r, seg); s = "esc " + h2(((op & 7) << 3) | m.reg) + ", " + rm16s(m); }
    else {
        switch (op) {
        case 0x80: case 0x82: { ModRM m = modrm(r, seg); s = std::string(ALU[m.reg]) + " " + rm8(m) + ", " + h2(r.b()); break; }
        case 0x81: { ModRM m = modrm(r, seg); s = std::string(ALU[m.reg]) + " " + rm16(m) + ", " + h4(r.w()); break; }
        case 0x83: { ModRM m = modrm(r, seg); const int8_t v = (int8_t)r.b();
                     s = std::string(ALU[m.reg]) + " " + rm16(m) + ", " + (v < 0 ? "-" + h2((unsigned)(-v)) : h2((unsigned)v)); break; }
        case 0x84: { ModRM m = modrm(r, seg); s = "test " + rm8s(m) + ", " + REG8[m.reg]; break; }
        case 0x85: { ModRM m = modrm(r, seg); s = "test " + rm16s(m) + ", " + REG16[m.reg]; break; }
        case 0x86: { ModRM m = modrm(r, seg); s = "xchg " + rm8s(m) + ", " + REG8[m.reg]; break; }
        case 0x87: { ModRM m = modrm(r, seg); s = "xchg " + rm16s(m) + ", " + REG16[m.reg]; break; }
        case 0x88: { ModRM m = modrm(r, seg); s = "mov " + rm8s(m) + ", " + REG8[m.reg]; break; }
        case 0x89: { ModRM m = modrm(r, seg); s = "mov " + rm16s(m) + ", " + REG16[m.reg]; break; }
        case 0x8A: { ModRM m = modrm(r, seg); s = std::string("mov ") + REG8[m.reg] + ", " + rm8s(m); break; }
        case 0x8B: { ModRM m = modrm(r, seg); s = std::string("mov ") + REG16[m.reg] + ", " + rm16s(m); break; }
        case 0x8C: { ModRM m = modrm(r, seg); s = "mov " + rm16s(m) + ", " + SREG[m.reg & 3]; break; }
        case 0x8D: { ModRM m = modrm(r, seg); s = std::string("lea ") + REG16[m.reg] + ", " + rm16s(m); break; }
        case 0x8E: { ModRM m = modrm(r, seg); s = std::string("mov ") + SREG[m.reg & 3] + ", " + rm16s(m); break; }
        case 0x8F: { ModRM m = modrm(r, seg); s = "pop " + rm16(m); break; }
        case 0x98: s = "cbw"; break;
        case 0x99: s = "cwd"; break;
        case 0x9A: { const uint16_t ip = r.w(); s = "call " + h4(r.w()) + ":" + h4(ip); break; }
        case 0x9B: s = "wait"; break;
        case 0x9C: s = "pushf"; break;
        case 0x9D: s = "popf"; break;
        case 0x9E: s = "sahf"; break;
        case 0x9F: s = "lahf"; break;
        case 0xA0: s = "mov al, " + std::string(seg.empty() ? "" : sseg) + "[" + h4(r.w()) + "]"; break;
        case 0xA1: s = "mov ax, " + std::string(seg.empty() ? "" : sseg) + "[" + h4(r.w()) + "]"; break;
        case 0xA2: s = "mov " + sseg + "[" + h4(r.w()) + "], al"; break;
        case 0xA3: s = "mov " + sseg + "[" + h4(r.w()) + "], ax"; break;
        case 0xA4: s = "movsb"; break;
        case 0xA5: s = "movsw"; break;
        case 0xA6: s = "cmpsb"; break;
        case 0xA7: s = "cmpsw"; break;
        case 0xA8: s = "test al, " + h2(r.b()); break;
        case 0xA9: s = "test ax, " + h4(r.w()); break;
        case 0xAA: s = "stosb"; break;
        case 0xAB: s = "stosw"; break;
        case 0xAC: s = "lodsb"; break;
        case 0xAD: s = "lodsw"; break;
        case 0xAE: s = "scasb"; break;
        case 0xAF: s = "scasw"; break;
        case 0xC0: case 0xC2: s = "ret " + h4(r.w()); break;
        case 0xC1: case 0xC3: s = "ret"; break;
        case 0xC4: { ModRM m = modrm(r, seg); s = std::string("les ") + REG16[m.reg] + ", " + rm16s(m); break; }
        case 0xC5: { ModRM m = modrm(r, seg); s = std::string("lds ") + REG16[m.reg] + ", " + rm16s(m); break; }
        case 0xC6: { ModRM m = modrm(r, seg); s = "mov " + rm8(m) + ", " + h2(r.b()); break; }
        case 0xC7: { ModRM m = modrm(r, seg); s = "mov " + rm16(m) + ", " + h4(r.w()); break; }
        case 0xC8: case 0xCA: s = "retf " + h4(r.w()); break;
        case 0xC9: case 0xCB: s = "retf"; break;
        case 0xCC: s = "int3"; break;
        case 0xCD: s = "int " + h2(r.b()); break;
        case 0xCE: s = "into"; break;
        case 0xCF: s = "iret"; break;
        case 0xD0: { ModRM m = modrm(r, seg); s = std::string(SHIFT[m.reg]) + " " + rm8(m) + ", 1"; break; }
        case 0xD1: { ModRM m = modrm(r, seg); s = std::string(SHIFT[m.reg]) + " " + rm16(m) + ", 1"; break; }
        case 0xD2: { ModRM m = modrm(r, seg); s = std::string(SHIFT[m.reg]) + " " + rm8(m) + ", cl"; break; }
        case 0xD3: { ModRM m = modrm(r, seg); s = std::string(SHIFT[m.reg]) + " " + rm16(m) + ", cl"; break; }
        case 0xD4: { const uint8_t b = r.b(); s = (b == 10) ? "aam" : "aam " + h2(b); break; }
        case 0xD5: { const uint8_t b = r.b(); s = (b == 10) ? "aad" : "aad " + h2(b); break; }
        case 0xD6: s = "salc"; break;
        case 0xD7: s = "xlat"; break;
        case 0xE0: s = "loopne " + rel8(); break;
        case 0xE1: s = "loope " + rel8(); break;
        case 0xE2: s = "loop " + rel8(); break;
        case 0xE3: s = "jcxz " + rel8(); break;
        case 0xE4: s = "in al, " + h2(r.b()); break;
        case 0xE5: s = "in ax, " + h2(r.b()); break;
        case 0xE6: s = "out " + h2(r.b()) + ", al"; break;
        case 0xE7: s = "out " + h2(r.b()) + ", ax"; break;
        case 0xE8: s = "call " + rel16(); break;
        case 0xE9: s = "jmp " + rel16(); break;
        case 0xEA: { const uint16_t ip = r.w(); s = "jmp " + h4(r.w()) + ":" + h4(ip); break; }
        case 0xEB: s = "jmp short " + rel8(); break;
        case 0xEC: s = "in al, dx"; break;
        case 0xED: s = "in ax, dx"; break;
        case 0xEE: s = "out dx, al"; break;
        case 0xEF: s = "out dx, ax"; break;
        case 0xF4: s = "hlt"; break;
        case 0xF5: s = "cmc"; break;
        case 0xF6: { ModRM m = modrm(r, seg); s = std::string(GRP3[m.reg]) + " " + rm8(m);
                     if (m.reg < 2) s += ", " + h2(r.b());
                     break; }
        case 0xF7: { ModRM m = modrm(r, seg); s = std::string(GRP3[m.reg]) + " " + rm16(m);
                     if (m.reg < 2) s += ", " + h4(r.w());
                     break; }
        case 0xF8: s = "clc"; break;
        case 0xF9: s = "stc"; break;
        case 0xFA: s = "cli"; break;
        case 0xFB: s = "sti"; break;
        case 0xFC: s = "cld"; break;
        case 0xFD: s = "std"; break;
        case 0xFE: { ModRM m = modrm(r, seg); s = std::string(GRP5[m.reg]) + " " + rm8(m); break; }
        case 0xFF: { ModRM m = modrm(r, seg); s = std::string(GRP5[m.reg]) + " " + rm16(m); break; }
        default: s = "db " + h2(op); break;
        }
    }

    if (r.short_of) {
        *output = "?";
        return 1;
    }
    *output = prefix + s;
    return r.pos;
}
