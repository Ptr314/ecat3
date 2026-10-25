// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Microprogrammed processor (МПУ) of the 15ИЭ-00-013 terminal

#pragma once

#include "emulator/core.h"

// МПУ терминала 15ИЭ-00-013 - процессор на рассыпной логике (плата
// ЩЦМ3.662.752): 3,08 МГц, одна команда за такт (ТО 15ИЭ, 5.2), 4 КБ ПЗУ
// программ, 8-разрядный аккумулятор, 32 регистра по 12 разрядов в двух
// страницах (старшие 4 разряда - страница адреса возврата), перенос и
// буфер АЛУ (БАЛУ) с триггером «A=B» (все выходы АЛУ - единицы, то есть
// результат 377). Система команд - таблица 7 ТО, поведение - по ядру MAME
// (src/devices/cpu/ie15), кроме БАЛУ.
//
// БАЛУ - по таблице команд Alex_K (zx-pk.ru, тема 36791; ТО 6.1.1.2:
// на канал данных выходит буфер АЛУ, а не аккумулятор). Результат команды
// АЛУ ложится в БАЛУ и оттуда - в аккумулятор или регистр. В БАЛУ пишут ADD,
// RAL, RAR, DSR, 05x, LLA, AND, XOR, LDA и CS (A - R - 1); LDC, LCA, переходы,
// флаги, STA и OTA его не трогают. STA и OTA выводят БАЛУ: после DSR или CS в
// регистр уйдёт их результат, после LCA - прежний. Прошивка на это
// рассчитывает: «влево» в служебной строке - DSR R9 (0x544), переход на 0x512 и
// там STA R5 сохраняет новую позицию маркера (Alex_K). С аккумулятором, как в
// MAME, туда попадало 26 из LCA, и маркер шёл по одному знаку, вставая между
// разрядами.
//
// Внешний мир виден через маппер: lla n / ota n - порты 00-17, а условия
// sfc/sfs 0-4 и флаги cfl/sfl 0-7 - порты 20-27 (флаг пишется 0 или 1).
// Особые случаи у самого процессора: порт 07 читается как 377, ota 16 и 17
// выбирают страницу регистров, условие 5 - разряд 7 БАЛУ, 6 - перенос, 7 -
// ложь; cfl/sfl 5 снимают перенос.
//
// Терминал - отдельный блок: сброс ЭВМ (тёплый) его не трогает, процессор
// начинает с нуля только при включении питания.
//
// Команда - такт, 3 миллиона за секунду, и проход цикла эмуляции на каждую
// стоил бы больше самих команд. Поэтому execute() выполняет пакет: подряд
// команды, которые к портам не обращаются, до следующей портовой (её - уже в
// следующем пакете, первой). Устройства терминала меняются только через
// порты, а такты пакета им отдаются разом, так что портовая команда видит
// развёртку, линию и клавиатуру ровно такими, как при покомандном счёте.
// Сдвигается лишь то, когда на этот домен смотрит другой процессор, - не
// больше чем на пакет. Под отладчиком - по одной команде.
class IE15CPU: public CPU
{
private:
    uint16_t m_pc = 0;
    uint8_t m_a = 0;
    uint8_t m_b = 0;                // БАЛУ
    bool m_cf = false;
    bool m_zf = false;              // «A=B», пишется вместе с БАЛУ
    bool m_rf = false;              // страница регистров (D30)
    uint16_t m_regs[32];

    static const unsigned BATCH = 64;   // команд в пакете, не больше
    static bool port_op(unsigned op);   // команда обращается к порту маппера

    uint8_t fetch();
    uint8_t arg() { return (uint8_t)mm->read(m_pc) & 0xFF; }
    uint8_t reg_lo(unsigned r) const { return (uint8_t)m_regs[(m_rf ? 16 : 0) + r]; }
    uint16_t reg(unsigned r) const { return m_regs[(m_rf ? 16 : 0) + r]; }
    void set_reg(unsigned r, uint16_t v) { m_regs[(m_rf ? 16 : 0) + r] = v & 0xFFF; }
    void set_b(uint8_t v) { m_b = v; m_zf = (v == 0xFF); }
    void set_a(uint8_t v) { m_a = v; set_b(v); }
    void skip();
    void execute_one(uint8_t op);
    void core_reset();

protected:
    unsigned int get_pc() override { return m_pc; }

public:
    IE15CPU(InterfaceManager *im, EmulatorConfigDevice *cd);

    void reset(bool cold) override;
    unsigned int execute() override;
    unsigned int read_mem(unsigned int address) override;
    void write_mem(unsigned int address, unsigned int data) override;
    unsigned int get_command() override;

    std::vector<std::pair<std::string, std::string>> get_registers() override;
    std::vector<std::pair<std::string, std::string>> get_flags() override;
    void set_context_value(const std::string &name, unsigned int value) override;
    bool set_flag(const std::string &name, bool value) override;
    unsigned int register_radix() const override { return 8; }
    DisAsm * create_disasm(const std::string &data_path, emulator::Result &res) override;

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_ie15_cpu(InterfaceManager *im, EmulatorConfigDevice *cd);
