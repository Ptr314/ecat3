// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: PDP-11 (К1801ВМ1) disassembler

#include "disasm_pdp11.h"
#include "emulator/utils.h"

namespace {

const char * REG_NAMES[8] = {"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"};

// Double operand instructions, indexed by bits 12-14
const char * DOUBLE_W[8] = {"", "MOV",  "CMP",  "BIT",  "BIC",  "BIS",  "ADD", ""};
const char * DOUBLE_B[8] = {"", "MOVB", "CMPB", "BITB", "BICB", "BISB", "SUB", ""};

// Single operand instructions, indexed by (opcode >> 6) - 050
const char * SINGLE_W[16] = {
    "CLR", "COM", "INC", "DEC", "NEG", "ADC", "SBC", "TST",
    "ROR", "ROL", "ASR", "ASL", "MARK", "MFPI", "MTPI", "SXT"
};
const char * SINGLE_B[16] = {
    "CLRB", "COMB", "INCB", "DECB", "NEGB", "ADCB", "SBCB", "TSTB",
    "RORB", "ROLB", "ASRB", "ASLB", "MTPS", "MFPD", "MTPD", "MFPS"
};

const char * BRANCH_LO[8] = {"", "BR", "BNE", "BEQ", "BGE", "BLT", "BGT", "BLE"};
const char * BRANCH_HI[8] = {"BPL", "BMI", "BHI", "BLOS", "BVC", "BVS", "BCC", "BCS"};

// Numbers without leading zeros: MOV #1, R0, not MOV #000001, R0
std::string oct(unsigned int value)
{
    return oct_str(value & 0xFFFF, 1);
}

uint16_t word_at(CommandBytes bytes, unsigned int index)
{
    return (uint16_t)((*bytes)[index * 2] | ((*bytes)[index * 2 + 1] << 8));
}

} // namespace

DisAsmPDP11::DisAsmPDP11(bool has_eis, Console console):
    has_eis(has_eis),
    console(console)
{
    // Opcode word plus up to two operand words
    max_command_length = 6;
}

emulator::Result DisAsmPDP11::load_file(MAYBE_UNUSED const std::string &file_name)
{
    return emulator::Result::ok();
}

std::string DisAsmPDP11::format_operand(unsigned int spec, CommandBytes bytes,
                                        unsigned int PC, unsigned int & index)
{
    unsigned int mode = (spec >> 3) & 7;
    unsigned int reg  = spec & 7;
    bool is_pc = (reg == 7);

    switch (mode) {
    case 0:
        return REG_NAMES[reg];
    case 1:
        return std::string("(") + REG_NAMES[reg] + ")";
    case 2:
        if (is_pc) {                                    // immediate
            uint16_t x = word_at(bytes, index++);
            return "#" + oct(x);
        }
        return std::string("(") + REG_NAMES[reg] + ")+";
    case 3:
        if (is_pc) {                                    // absolute
            uint16_t x = word_at(bytes, index++);
            return "@#" + oct(x);
        }
        return std::string("@(") + REG_NAMES[reg] + ")+";
    case 4:
        return std::string("-(") + REG_NAMES[reg] + ")";
    case 5:
        return std::string("@-(") + REG_NAMES[reg] + ")";
    case 6: {
        uint16_t x = word_at(bytes, index++);
        if (is_pc)                                      // PC relative, show the target
            return oct(PC + 2 * index + x);
        return oct(x) + "(" + REG_NAMES[reg] + ")";
    }
    default: {
        uint16_t x = word_at(bytes, index++);
        if (is_pc)
            return "@" + oct(PC + 2 * index + x);
        return "@" + oct(x) + "(" + REG_NAMES[reg] + ")";
    }
    }
}

// The address a JMP or a JSR goes to, when the operand names it: relative to
// PC (mode 67) or absolute (37). The others go through a register or a cell
static bool operand_target(unsigned int spec, CommandBytes bytes, unsigned int PC,
                           unsigned int first, unsigned int index, unsigned int &target)
{
    if ((spec & 7) != 7 || index == first) return false;
    const uint16_t x = word_at(bytes, first);
    switch ((spec >> 3) & 7) {
    case 3: target = x; return true;
    case 6: target = (PC + 2 * index + x) & 0xFFFF; return true;
    default: return false;
    }
}

unsigned int DisAsmPDP11::decode(CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out)
{
    *out = DisasmLine();
    if (max_len < 2) { out->text = "?"; out->length = 2; return 2; }
    std::string * output = &out->text;
    bool valid = true;

    uint16_t cmd = word_at(bytes, 0);
    unsigned int index = 1;                             // next unread word
    std::string mnemonic;
    std::string operands;

    unsigned int group = (cmd >> 12) & 017;

    if ((group >= 1 && group <= 6) || (group >= 011 && group <= 016)) {
        // Double operand. 16SSDD is SUB, not a byte form of ADD.
        bool is_byte = (group >= 011) && (group != 016);
        mnemonic = is_byte? DOUBLE_B[group & 07] : DOUBLE_W[group & 07];
        if (group == 016) mnemonic = "SUB";
        std::string src = format_operand((cmd >> 6) & 077, bytes, PC, index);
        std::string dst = format_operand(cmd & 077, bytes, PC, index);
        operands = src + ", " + dst;
    }
    else if ((cmd & 0177400) >= 0000400 && (cmd & 0177400) <= 0003400) {
        mnemonic = BRANCH_LO[(cmd >> 8) & 7];
        int8_t offset = (int8_t)(cmd & 0xFF);
        operands = oct(PC + 2 + offset * 2);
        out->flow = FLOW_JUMP;
        out->conditional = (cmd & 0177400) != 0000400;
        out->has_target = true;
        out->target = (PC + 2 + offset * 2) & 0xFFFF;
    }
    else if ((cmd & 0177400) >= 0100000 && (cmd & 0177400) <= 0103400) {
        mnemonic = BRANCH_HI[(cmd >> 8) & 7];
        int8_t offset = (int8_t)(cmd & 0xFF);
        operands = oct(PC + 2 + offset * 2);
        out->flow = FLOW_JUMP;
        out->conditional = true;
        out->has_target = true;
        out->target = (PC + 2 + offset * 2) & 0xFFFF;
    }
    else if ((cmd & 0177400) == 0104000) {
        mnemonic = "EMT";
        operands = oct(cmd & 0377);
        out->flow = FLOW_CALL;
    }
    else if ((cmd & 0177400) == 0104400) {
        mnemonic = "TRAP";
        operands = oct(cmd & 0377);
        out->flow = FLOW_CALL;
    }
    else if ((cmd & 0177000) == 0004000) {
        // JSR PC is written CALL, with the link register left out
        const unsigned int first = index;
        const unsigned int reg = (cmd >> 6) & 7;
        mnemonic = reg == 7? "CALL" : "JSR";
        operands = format_operand(cmd & 077, bytes, PC, index);
        if (reg != 7) operands = std::string(REG_NAMES[reg]) + ", " + operands;
        out->flow = FLOW_CALL;
        out->has_target = operand_target(cmd & 077, bytes, PC, first, index, out->target);
    }
    else if ((cmd & 0177000) == 0074000) {
        mnemonic = "XOR";
        operands = std::string(REG_NAMES[(cmd >> 6) & 7]) + ", "
                 + format_operand(cmd & 077, bytes, PC, index);
    }
    else if ((cmd & 0177000) == 0077000) {
        mnemonic = "SOB";
        operands = std::string(REG_NAMES[(cmd >> 6) & 7]) + ", "
                 + oct(PC + 2 - (cmd & 077) * 2);
        out->flow = FLOW_JUMP;
        out->conditional = true;
        out->has_target = true;
        out->target = (PC + 2 - (cmd & 077) * 2) & 0xFFFF;
    }
    else if ((cmd & 0174000) == 0070000) {
        static const char * EIS[4] = {"MUL", "DIV", "ASH", "ASHC"};
        if (!has_eis) {
            mnemonic = ".WORD";
            operands = oct(cmd);
            valid = false;
        } else {
            mnemonic = EIS[(cmd >> 9) & 3];
            operands = format_operand(cmd & 077, bytes, PC, index) + ", "
                     + REG_NAMES[(cmd >> 6) & 7];
        }
    }
    else if ((cmd & 0177700) == 0000100) {
        mnemonic = "JMP";
        const unsigned int first = index;
        operands = format_operand(cmd & 077, bytes, PC, index);
        out->flow = FLOW_JUMP;
        out->has_target = operand_target(cmd & 077, bytes, PC, first, index, out->target);
        // JMP to a register is an illegal instruction
        valid = ((cmd >> 3) & 7) != 0;
    }
    else if ((cmd & 0177700) == 0000300) {
        mnemonic = "SWAB";
        operands = format_operand(cmd & 077, bytes, PC, index);
    }
    else if ((cmd & 0177770) == 0000200) {
        // RTS PC is written RET
        if ((cmd & 7) == 7) mnemonic = "RET";
        else {
            mnemonic = "RTS";
            operands = REG_NAMES[cmd & 7];
        }
        out->flow = FLOW_RET;
    }
    else if ((cmd & 0177740) == 0000240) {
        if ((cmd & 037) == 0) mnemonic = "NOP";
        else {
            // A single flag has its own mnemonic, a combination is spelled out
            static const char * FLAG_NAMES[4] = {"C", "V", "Z", "N"};
            std::string flags;
            for (int i = 3; i >= 0; i--)
                if ((cmd & (1 << i)) != 0) flags += FLAG_NAMES[i];
            mnemonic = ((cmd & 020) != 0? "SE" : "CL") + flags;
        }
    }
    else if (((cmd >> 6) & 0777) >= 050 && ((cmd >> 6) & 0777) <= 067) {
        unsigned int sop = ((cmd >> 6) & 0777) - 050;
        bool is_byte = (cmd & 0100000) != 0;
        mnemonic = is_byte? SINGLE_B[sop] : SINGLE_W[sop];
        if (!is_byte && sop == 014)                     // MARK takes a count
            operands = oct(cmd & 077);
        else
            operands = format_operand(cmd & 077, bytes, PC, index);
    }
    else {
        static const char * NO_OPERAND[8] = {
            "HALT", "WAIT", "RTI", "BPT", "IOT", "RESET", "RTT", ".WORD"
        };
        // Console instructions, decoded in ranges as the chips do
        // (bkbtl-doc «1801vm1 vs 1801vm2»); 000030 is reserved on the ВМ2,
        // 000020-000037 on the ВМ1, the whole block on the ВМ3. The ВМ2 names
        // are those the УК-НЦ ROM declares for its monitor ($RUN$ 12, $STEP$ 16,
        // $MFPM$ 21, $MFPC$ 22, $MFPS$ 24, $MTPM$ 31, $MTPC$ 32, $MTPS$ 34);
        // it has none for 000020, which keeps the bkbtl-doc RSEL. MFPS and
        // MTPS here are not the byte instructions 1067DD / 1064SS
        static const char * CONSOLE_VM2_NAMES[030] = {
            "RUN",   "RUN",   "RUN",   "RUN",   "STEP", "STEP", "STEP", "STEP",     // 10-17
            "RSEL",  "MFPM",  "MFPC",  "MFPC",  "MFPS", "MFPS", "MFPS", "MFPS",     // 20-27
            nullptr, "MTPM",  "MTPC",  "MTPC",  "MTPS", "MTPS", "MTPS", "MTPS"      // 30-37
        };
        const char * console_name = nullptr;
        if (cmd >= 010 && cmd <= 037) {
            if (console == CONSOLE_VM2)
                console_name = CONSOLE_VM2_NAMES[cmd - 010];
            else if (console == CONSOLE_VM1 && cmd <= 017)
                console_name = (cmd < 014) ? "START" : "STEP";  // the ВМ1 description's names
        }

        if (cmd <= 6) {
            mnemonic = NO_OPERAND[cmd];
            if (cmd == 2 || cmd == 6) out->flow = FLOW_RET;           // RTI, RTT
            else if (cmd == 3 || cmd == 4) out->flow = FLOW_CALL;     // BPT, IOT
        }
        else if (console_name != nullptr)
            mnemonic = console_name;
        else {
            mnemonic = ".WORD";
            operands = oct(cmd);
            valid = false;
        }
    }

    if (operands.empty()) *output = mnemonic;
    else *output = mnemonic + " " + operands;

    unsigned int length = index * 2;
    if (length > max_len) { length = max_len; valid = false; }
    out->length = length;
    out->valid = valid;
    return length;
}
