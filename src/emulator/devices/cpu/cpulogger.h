// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: CPU logger class

#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/utils.h"
#include "emulator/devices/cpu/i8080_context.h"

#ifndef EXTERNAL_Z80
#include "emulator/devices/cpu/z80_context.h"
#else
#include "libs/z80.hpp"
#endif


#define CPU_LOGGER_8080     1
#define CPU_LOGGER_Z80      2

class CPULogger
{
private:
    CPU * cpu;
    void * context;
    int cpu_type;
    unsigned int logged_count;
    std::string log;
    std::string log_name;

    static std::string dec_str(unsigned int value, int width)
    {
        std::string s = std::to_string(value);
        while (static_cast<int>(s.size()) < width) s = "0" + s;
        return s;
    }

    static std::string to_upper(const std::string &s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    static std::string current_time_string()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", std::localtime(&t));
        return std::string(buf);
    }

public:
    CPULogger(CPU * cpu, int cpu_type, void * context, const std::string &log_name):
        cpu(cpu),
        cpu_type(cpu_type),
        context(context),
        logged_count(0),
        log_name(log_name)
    {}

    void log_state(uint8_t command, bool before, unsigned int cycles=0)
    {
        unsigned int pc = cpu->get_pc();
        std::string cs;
        if (cpu_type == CPU_LOGGER_8080) {
            i8080context * c = static_cast<i8080context*>(context);
            cs =   hex_str(pc, 4)
                 + " " + hex_str(command, 2) + ((before)?"+":"-")
                 + ((!before)?(" " + dec_str(cycles, 2)):"   ")
                 + " AF:" + hex_str(c->registers.regs.A, 2) + hex_str(c->registers.regs.F, 2)
                 + " BC:" + hex_str(c->registers.reg_pairs.BC, 4)
                 + " DE:" + hex_str(c->registers.reg_pairs.DE, 4)
                 + " HL:" + hex_str(c->registers.reg_pairs.HL, 4)
                 + " SP:" + hex_str(c->registers.regs.SP, 4)
                 + "\x0D\x0A";

        } else
        if (cpu_type == CPU_LOGGER_Z80) {
#ifndef EXTERNAL_Z80
            z80context * c = static_cast<z80context*>(context);
            cs =   hex_str(pc, 4)
                 + " " + hex_str(command, 2) + ((before)?"+":"-")
                 + " AF:" + hex_str(c->registers.reg_pairs.AF, 4)
                 + " BC:" + hex_str(c->registers.reg_pairs.BC, 4)
                 + " DE:" + hex_str(c->registers.reg_pairs.DE, 4)
                 + " HL:" + hex_str(c->registers.reg_pairs.HL, 4)
                 + " SP:" + hex_str(c->registers.regs.SP, 4)
                 + " IX:" + hex_str(c->registers.reg_pairs.IX, 4)
                 + " IY:" + hex_str(c->registers.reg_pairs.IY, 4)
                 + "\x0D\x0A";
#else
            Z80 * c = static_cast<Z80*>(context);
            cs =   hex_str(pc, 4)
                 + " " + hex_str(command, 2) + ((before)?"+":"-")
                 + " AF:" + hex_str(c->reg.pair.A, 2) + hex_str(c->reg.pair.F, 2)
                 + " BC:" + hex_str(c->reg.pair.B, 2) + hex_str(c->reg.pair.C, 2)
                 + " DE:" + hex_str(c->reg.pair.D, 2) + hex_str(c->reg.pair.E, 2)
                 + " HL:" + hex_str(c->reg.pair.H, 2) + hex_str(c->reg.pair.L, 2)
                 + " SP:" + hex_str(c->reg.SP, 4)
                 + " IX:" + hex_str(c->reg.IX, 4)
                 + " IY:" + hex_str(c->reg.IY, 4)
                 + "\x0D\x0A";
#endif

        }
        log += cs;
        logged_count++;
    }

    void logs(const std::string &s)
    {
        log += s + "\x0D\x0A";
        logged_count++;
    }


    ~CPULogger()
    {
        std::string formattedTime = current_time_string();
        std::string log_file = log_name + "_" + formattedTime + ".log";
        std::cerr << "Logged " << logged_count << " entries" << std::endl;
        std::cerr << "Writing log to " << log_file << std::endl;
        std::ofstream ofs(log_file, std::ios::binary);
        if (ofs.is_open())
        {
            std::string upper_log = to_upper(log);
            ofs.write(upper_log.c_str(), upper_log.size());
            ofs.close();
        }
    }
};