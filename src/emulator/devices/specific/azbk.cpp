// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): memory mapper and board registers

#include <cstring>

#include "azbk.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

// Регистры платы, абсолютные адреса
static const unsigned int R_130   = 0177130;
static const unsigned int R_WIN   = 0177300;    // 177300-177336
static const unsigned int R_WIN_E = 0177337;
static const unsigned int R_ACT   = 0177340;
static const unsigned int R_RO    = 0177342;
static const unsigned int R_SHDW  = 0177344;
static const unsigned int R_CTRL  = 0177346;
static const unsigned int R_C130  = 0177350;
static const unsigned int R_C716  = 0177352;
static const unsigned int R_VER   = 0177370;
static const unsigned int R_716   = 0177716;
static const unsigned int IO_PAGE = 0177000;    // окна кончаются на 176777

// Разряды 177346, которые программа не пишет: версия платы (0) и тип доработки
static const unsigned int CTRL_READ_ONLY = AZBK::CTRL_REV037 | 3u;

// Окна 8-11 при включении ПЗУ БК0011М через 177716 (Verilog MAXIOL, 5.2.2):
// разряд 1 - 327 в 100000 и 325 в 120000, разряд 0 - 328 и 329
static const unsigned int ROM9_PAGES[4] = { 0124, 0125, 0122, 0123 };
static const unsigned int ROM8_PAGES[4] = { 0126, 0127, 0130, 0131 };

AZBK::AZBK(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    i_bk_sys(this, im, 5, "bk_sys", MODE_R)
{
    addresable_size = 0200000;
    can_read = true;
    can_write = true;
}

emulator::Result AZBK::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    const std::string bad = "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name;

    m_ram = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    // Не меньше страниц СМК (0377): ниже ими пользуется сам контроллер
    if (m_ram == nullptr || !m_ram->is_plain() || m_ram->get_size() < 0400 * PAGE_BYTES)
        return emulator::Result::error(emulator::ErrorCode::ConfigError, bad);
    m_buf = m_ram->get_buffer();
    m_ram_size = m_ram->get_size();
    m_page_gen.assign(PAGES, 0);

    if (cd->get_parameter("machine").value != "bk11")
        return emulator::Result::error(emulator::ErrorCode::ConfigError, bad);

    const std::string mod = cd->get_parameter("modification", false).value;
    if (mod == "vp037") m_vp037 = true;
    else if (mod.empty() || mod == "none") m_vp037 = false;
    else return emulator::Result::error(emulator::ErrorCode::ConfigError, bad);

    m_fpga_version = read_confg_value(cd, "fpga_version", true, 0u) & 0177777;

    // Слоты ПЗУ: rom_NN, NN - десятичный номер слота R00..R63
    m_slots.clear();
    for (const auto &p : cd->parameters) {
        if (p.name.compare(0, 4, "rom_") != 0) continue;
        unsigned int slot = 0;
        try {
            slot = (unsigned int)std::stoul(p.name.substr(4), nullptr, 10);
        } catch (std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (" + p.name + ")");
        }
        if (slot >= ROM_PAGES || str_trim(p.value).empty())
            return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (" + p.name + ")");
        m_slots.push_back({slot, str_trim(p.value)});
    }
    m_logo = str_trim(cd->get_parameter("logo", false).value);

    // Файлы читаются сразу: нет файла или слоты перекрываются - ошибка
    // конфигурации, а не пустое ПЗУ после сброса
    std::vector<bool> used(ROM_PAGES, false);
    for (const Slot &s : m_slots) {
        const std::string file = find_file_location(sd, s.file);
        const long long size = file.empty() ? -1 : dsk_tools::utf8_file_size(file);
        if (size <= 0)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ROM|" + std::string(QT_TRANSLATE_NOOP("ROM", "File not found")) + "} " + s.file);
        const unsigned int pages = (unsigned int)((size + PAGE_BYTES - 1) / PAGE_BYTES);
        for (unsigned int i = 0; i < pages; i++) {
            if (s.slot + i >= ROM_PAGES || used[s.slot + i])
                return emulator::Result::error(emulator::ErrorCode::ConfigError, bad + " (rom_" + std::to_string(s.slot) + ")");
            used[s.slot + i] = true;
        }
    }

    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    return emulator::Result::ok();
}

// Микроконтроллер грузит ПЗУ из AZ.INI в страницы 0100+, пока держит БК
// низким HALT. Остаток страницы за коротким файлом - нули
void AZBK::load_slots()
{
    if (m_buf == nullptr) return;
    memset(m_buf + ROM_PAGE * PAGE_BYTES, 0, ROM_PAGES * PAGE_BYTES);
    for (const Slot &s : m_slots) {
        const std::string data = dsk_tools::utf8_read_file(find_file_location(sd, s.file));
        const size_t at = (ROM_PAGE + s.slot) * PAGE_BYTES;
        const size_t room = (ROM_PAGE + ROM_PAGES) * PAGE_BYTES - at;
        memcpy(m_buf + at, data.data(), std::min(data.size(), room));
    }
    if (!m_logo.empty()) {
        const std::string data = dsk_tools::utf8_read_file(find_file_location(sd, m_logo));
        const size_t room = (ROM_PAGE - LOGO_PAGE) * PAGE_BYTES;
        memcpy(m_buf + LOGO_PAGE * PAGE_BYTES, data.data(), std::min(data.size(), room));
    }
}

void AZBK::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        // При включении все регистры нулевые: и разряды 6-8 177346, по
        // которым стартовое ПЗУ делает первый проход с перезапуском
        memset(m_win, 0, sizeof(m_win));
        m_active = m_ro = m_shadow = 0;
        m_ctrl = 0;
        m_copy130 = m_copy716 = 0;
        memset(m_pre_smk_win, 0, sizeof(m_pre_smk_win));
        m_pre_smk_active = m_pre_smk_ro = 0;
        memset(m_pre_b11m_win, 0, sizeof(m_pre_b11m_win));
        m_pre_b11m_active = m_pre_b11m_shadow = false;
        m_last716 = 0;
    }
    // Сброс БК для контроллера - низкий HALT: микроконтроллер грузит ПЗУ
    // заново и ставит стартовую конфигурацию
    restart();
}

void AZBK::restart()
{
    load_slots();
    touch_all();
    halt1();
}

// STATE_HLT_HALT1 (Verilog MAXIOL, 7.2): стартовое ПЗУ в окне 15, отвечает
// только оно; 177342 и 177344 остаются как были
void AZBK::halt1()
{
    m_halts++;
    m_win[15] = ROM_PAGE;
    m_active = 0100000;
    m_ctrl &= ~(CTRL_WIN1OFF | CTRL_V100 | CTRL_014OFF | CTRL_037OFF);
    if (m_vp037) m_ctrl = (m_ctrl | CTRL_REV037) & ~CTRL_WIN1MOD;
    else m_ctrl &= ~CTRL_REV037;
    m_ctrl &= ~3u;
    m_smk_flag = false;
    m_start = true;
    routes_changed();
}

//----------------------------------------------------------------- окна ---

bool AZBK::window_ro(unsigned int n) const
{
    const unsigned int page = m_win[n];
    return ((m_ro >> n) & 1) != 0 || (page >= ROM_PAGE && page < ROM_PAGE + ROM_PAGES);
}

// Доработка 037 отключает ОЗУ БК 000000-137777 (разряд 9 177346, при разряде
// 14 = 1). ПЗУ в окне 100000 стоят не за 037 и остаются
bool AZBK::bk_memory_off(unsigned int n) const
{
    if (n >= 12 || !m_vp037 || (m_ctrl & CTRL_037OFF) == 0) return false;
    if (n >= 8 && (i_bk_sys.value & 033) != 0) return false;
    return true;
}

bool AZBK::bk_ram_under(unsigned int n) const
{
    if (n >= 12) return false;
    if (n >= 8 && (i_bk_sys.value & 033) != 0) return false;
    return !bk_memory_off(n);
}

uint32_t AZBK::route_key(unsigned int n) const
{
    return ((m_active >> n) & 1)
         | (((m_shadow >> n) & 1) << 1)
         | ((window_ro(n) ? 1u : 0u) << 2)
         | ((bk_memory_off(n) ? 1u : 0u) << 3);
}

void AZBK::routes_changed()
{
    bool changed = false;
    for (unsigned int n = 0; n < 16; n++) {
        const uint32_t k = route_key(n);
        if (k != m_route_key[n]) { m_route_key[n] = k; changed = true; }
    }
    if (changed && m_mapper != nullptr) m_mapper->routing_changed();
}

// Под окном n у БК0011М: 0-7 - ОЗУ, 8-11 - ОЗУ или ПЗУ по разрядам 0-4
// 177716 (3, 4 - внешние ПЗУ, которых нет), 12-13 - ПЗУ БОС, 14-15 - ничего
unsigned int AZBK::route(unsigned int address, unsigned int mode)
{
    const unsigned int n = (address >> 12) & 15;
    const bool active = ((m_active >> n) & 1) != 0;
    const bool off = bk_memory_off(n);
    if (mode != MODE_W) {
        if (!active) return off ? ROUTE_TIMEOUT : ROUTE_PASS;
        bool bk_answers;
        if (n < 8) bk_answers = !off;
        else if (n < 12) bk_answers = (i_bk_sys.value & 3) != 0 || ((i_bk_sys.value & 030) == 0 && !off);
        else bk_answers = n < 14;
        // Отвечают оба: на шине данные складываются
        return bk_answers ? ROUTE_OR : ROUTE_ANSWER;
    }
    if (active) {
        // Окно только для чтения запись не принимает: ее берет то, что под
        // ним, если есть (так у gid; ответ платы - вопрос Б.12)
        if (window_ro(n)) return off ? ROUTE_TIMEOUT : ROUTE_PASS;
        return bk_ram_under(n) ? ROUTE_THROUGH : ROUTE_ANSWER;
    }
    // Тень: запись в память БК копируется в страницу окна, RPLY не дает
    if (((m_shadow >> n) & 1) != 0 && !window_ro(n)) return ROUTE_SNOOP;
    return off ? ROUTE_TIMEOUT : ROUTE_PASS;
}

//------------------------------------------------------- 177716 и 177130 ---

// Запись в 177716 (Verilog MAXIOL, 5.2): принимается с разрядом 11 на
// БК0011М или на БК0010 с заменой памяти 037; окна 0-11 переписываются по
// номерам страниц БК11М
void AZBK::write_716(unsigned int w)
{
    w &= 0177777;
    if ((w & 04000) == 0) return;
    const bool bk10 = (m_ctrl & CTRL_BK10) != 0;
    if (bk10 && !((m_ctrl & CTRL_037OFF) && (m_ctrl & CTRL_REV037))) return;
    m_copy716 = w;

    const unsigned int win0 = (w >> 12) & 7;
    const unsigned int win1 = (w >> 8) & 7;
    const bool roms01 = (w & 3) != 0;
    const bool roms34 = (w & 030) != 0;
    const bool c9 = (m_ctrl & CTRL_037OFF) != 0;

    for (unsigned int i = 0; i < 4; i++)
        m_pre_b11m_win[i] = (w & 2) ? ROM9_PAGES[i] : (w & 1) ? ROM8_PAGES[i] : (win1 << 2) + i;
    m_pre_b11m_active = roms01 ? (m_ctrl & CTRL_ROM11) != 0 : roms34 ? false : c9;
    m_pre_b11m_shadow = (roms01 || roms34) ? false : !c9;

    // Начальный блок 000000-037777 - всегда страница БК 0
    for (unsigned int i = 0; i < 4; i++) {
        m_win[i] = 030 + i;
        m_win[4 + i] = (win0 << 2) + i;
    }
    m_active = (m_active & ~0377u) | (c9 ? 0377u : 0u);
    m_ro &= ~0377u;
    m_shadow = (m_shadow & ~0377u) | (c9 ? 0u : 0377u);

    if ((m_ctrl & CTRL_WIN1OFF) == 0) {
        for (unsigned int i = 0; i < 4; i++) m_win[8 + i] = m_pre_b11m_win[i];
        m_active = (m_active & ~07400u) | (m_pre_b11m_active ? 07400u : 0u);
        m_ro &= ~07400u;
        m_shadow = (m_shadow & ~07400u) | (m_pre_b11m_shadow ? 07400u : 0u);
    } else {
        // Окно 1 отдано эмуляции СМК
        for (unsigned int i = 0; i < 4; i++) m_win[8 + i] = m_pre_smk_win[i];
        m_active = (m_active & ~07400u) | ((m_pre_smk_active & 017) << 8);
        m_ro = (m_ro & ~07400u) | ((m_pre_smk_ro & 017) << 8);
        m_shadow &= ~07400u;
    }
    routes_changed();
}

// Запись в 177130 (Verilog MAXIOL, 6): запись ровно 6 ставит флаг, следующая
// запись - режим (разряды 6-4) и страница {10,3,2,0} в страницах 0200-0377
void AZBK::write_130(unsigned int w)
{
    w &= 0177777;
    if (!m_smk_flag) {
        m_smk_flag = (w == 6);
        return;
    }
    m_smk_flag = false;
    m_copy130 = w;

    const unsigned int page = ((w >> 10) & 1) << 3 | ((w >> 3) & 1) << 2 | ((w >> 2) & 1) << 1 | (w & 1);
    const unsigned int base = 0200 | (page << 3);
    unsigned int *p = m_pre_smk_win;
    // Окна, которые режим не называет, сохраняют прежнее значение
    switch ((w >> 4) & 7) {
    case 7:     // Start: стартовое ПЗУ, ПЗУ КНГМД, страница
        p[7] = 0100; p[6] = 0110; p[5] = base + 1; p[4] = base; p[3] = base + 7; p[2] = base + 6;
        m_pre_smk_active = 0374; m_pre_smk_ro = 0;
        break;
    case 3:     // Std10
        p[7] = base + 7; p[6] = 0110; p[5] = base + 5; p[4] = base + 4; p[3] = base + 3; p[2] = base + 2;
        m_pre_smk_active = 0374; m_pre_smk_ro = 0;
        break;
    case 5:     // ОЗУ10
        for (unsigned int i = 0; i < 8; i++) p[i] = base + i;
        m_pre_smk_active = 0377; m_pre_smk_ro = 0;
        break;
    case 1:     // All
        p[7] = base + 3; p[6] = base + 2; p[5] = base + 1; p[4] = base;
        p[3] = base + 7; p[2] = base + 6; p[1] = base + 5; p[0] = base + 4;
        m_pre_smk_active = 0377; m_pre_smk_ro = 0;
        break;
    case 6:     // Std11: стандартное ПЗУ БК11М
        p[7] = base + 7; p[6] = 0110; p[5] = 0121; p[4] = 0120;
        m_pre_smk_active = 0360; m_pre_smk_ro = 0;
        break;
    case 2:     // ОЗУ11
        for (unsigned int i = 4; i < 8; i++) p[i] = base + i;
        m_pre_smk_active = 0360; m_pre_smk_ro = 0;
        break;
    case 4:     // HLT10: окно 100000 только на чтение
        for (unsigned int i = 0; i < 8; i++) p[i] = base + i;
        m_pre_smk_active = 0377; m_pre_smk_ro = 1;
        break;
    default:    // 0, HLT11
        for (unsigned int i = 4; i < 8; i++) p[i] = base + i;
        m_pre_smk_active = 0360; m_pre_smk_ro = 0;
        break;
    }

    for (unsigned int i = 0; i < 4; i++) m_win[12 + i] = p[4 + i];
    m_active = (m_active & ~0170000u) | ((m_pre_smk_active & 0360) << 8);
    m_ro = (m_ro & ~0170000u) | ((m_pre_smk_ro & 0360) << 8);
    m_shadow &= ~0170000u;          // вся память СМК - память AZ

    if (w & 020) {
        for (unsigned int i = 0; i < 4; i++) m_win[8 + i] = p[i];
        m_active = (m_active & ~07400u) | ((m_pre_smk_active & 017) << 8);
        m_ro = (m_ro & ~07400u) | ((m_pre_smk_ro & 017) << 8);
        m_shadow &= ~07400u;
        m_ctrl |= CTRL_WIN1OFF;
    } else {
        for (unsigned int i = 0; i < 4; i++) m_win[8 + i] = m_pre_b11m_win[i];
        m_active = (m_active & ~07400u) | (m_pre_b11m_active ? 07400u : 0u);
        m_ro &= ~07400u;
        m_shadow = (m_shadow & ~07400u) | (m_pre_b11m_shadow ? 07400u : 0u);
        m_ctrl &= ~CTRL_WIN1OFF;
    }
    routes_changed();
}

//------------------------------------------------------------- регистры ---

unsigned int AZBK::read_register(unsigned int address, bool direct)
{
    const unsigned int a = address & ~1u;
    unsigned int v = 0;
    if (a >= R_WIN && a <= R_WIN_E) v = m_win[(a - R_WIN) >> 1];
    else if (a == R_ACT)  v = m_active;
    else if (a == R_RO)   v = m_ro;
    else if (a == R_SHDW) v = m_shadow;
    else if (a == R_CTRL) v = m_ctrl;
    else if (a == R_C130) v = m_copy130;
    else if (a == R_C716) v = m_copy716;
    else if (a == R_VER)  v = m_fpga_version;
    else if (a == R_716) {
        // Verilog MAXIOL, 5.1: 15 и 7 - единицы, 14-12 - 170000 на старте или
        // 140000 при эмуляции БК11М с отключенной 037. Нажатие (разряд 6)
        // отдается только с PS/2 вместо 014 - ее нет
        v = 0100200;
        if (m_start) v |= 070000;
        else if ((m_ctrl & CTRL_REV037) && (m_ctrl & CTRL_037OFF) && (m_ctrl & CTRL_BK11EMU)) v |= 040000;
        if (!direct) m_start = false;
    } else
        return _FFFF;
    return v & 0177777;
}

void AZBK::write_register(unsigned int address, unsigned int value)
{
    const unsigned int a = address & ~1u;
    value &= 0177777;
    if (a >= R_WIN && a <= R_WIN_E) {
        m_win[(a - R_WIN) >> 1] = value & 017777;
    } else if (a == R_ACT) {
        m_active = value;
    } else if (a == R_RO) {
        m_ro = value;
    } else if (a == R_SHDW) {
        m_shadow = value;
    } else if (a == R_CTRL) {
        m_ctrl = (value & ~CTRL_READ_ONLY) | (m_ctrl & CTRL_READ_ONLY);
    } else if (a == R_716) {
        m_last716 = value;
        write_716(value);
        return;
    } else if (a == R_130) {
        write_130(value);
        return;
    } else {
        return;     // 177350, 177352, 177370 - только чтение
    }
    routes_changed();
}

// Байт в регистр: слово собирается с тем, что там было
static unsigned int merge_byte(unsigned int old, unsigned int address, unsigned int value)
{
    value &= 0xFF;
    return (address & 1) ? ((old & 0xFF) | (value << 8)) : ((old & 0xFF00) | value);
}

//------------------------------------------------------- обращения шины ---

unsigned int AZBK::get_value(unsigned int address)
{
    if (address < IO_PAGE) {
        const unsigned int a = m_win[address >> 12] * PAGE_BYTES + (address & 07777);
        return (a < m_ram_size) ? m_buf[a] : 0xFF;
    }
    const unsigned int v = read_register(address, false);
    if (v == _FFFF) return v;
    return (address & 1) ? (v >> 8) : (v & 0xFF);
}

unsigned int AZBK::get_direct(unsigned int address)
{
    if (address < IO_PAGE) return get_value(address);
    const unsigned int v = read_register(address, true);
    if (v == _FFFF) return v;
    return (address & 1) ? (v >> 8) : (v & 0xFF);
}

unsigned int AZBK::get_value_word(unsigned int address)
{
    if (address < IO_PAGE) {
        const unsigned int a = m_win[address >> 12] * PAGE_BYTES + (address & 07776);
        return (a + 1 < m_ram_size) ? (m_buf[a] | (m_buf[a + 1] << 8)) : 0177777;
    }
    return read_register(address, false);
}

void AZBK::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    if (address < IO_PAGE) {
        const unsigned int n = address >> 12;
        if (window_ro(n)) return;
        const unsigned int a = m_win[n] * PAGE_BYTES + (address & 07777);
        if (a < m_ram_size) {
            m_buf[a] = (uint8_t)value;
            m_page_gen[m_win[n]]++;
        }
        return;
    }
    const unsigned int a = address & ~1u;
    unsigned int old;
    if (a == R_716) old = m_last716;
    else if (a == R_130) old = 0;
    else {
        old = read_register(address, true);
        if (old == _FFFF) return;
    }
    write_register(address, merge_byte(old, address, value));
}

void AZBK::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    if (address < IO_PAGE) {
        const unsigned int n = address >> 12;
        if (window_ro(n)) return;
        const unsigned int a = m_win[n] * PAGE_BYTES + (address & 07776);
        if (a + 1 < m_ram_size) {
            m_buf[a] = (uint8_t)value;
            m_buf[a + 1] = (uint8_t)(value >> 8);
            m_page_gen[m_win[n]]++;
        }
        return;
    }
    write_register(address, value);
}

// Память окон; регистры отладчик только показывает
bool AZBK::debug_writable(unsigned int address) const
{
    return address < IO_PAGE && !window_ro(address >> 12);
}

//---------------------------------------------------------------- снимок ---

void AZBK::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.array("windows", m_win, 16);
    w.u("active", m_active, 16);
    w.u("readonly", m_ro, 16);
    w.u("shadow", m_shadow, 16);
    w.u("control", m_ctrl, 16);
    w.u("copy130", m_copy130, 16);
    w.u("copy716", m_copy716, 16);
    w.u("last716", m_last716, 16);
    w.b("start", m_start);
    w.b("smk_flag", m_smk_flag);
    w.array("smk_windows", m_pre_smk_win, 8);
    w.u("smk_active", m_pre_smk_active, 8);
    w.u("smk_readonly", m_pre_smk_ro, 8);
    w.array("b11m_windows", m_pre_b11m_win, 4);
    w.b("b11m_active", m_pre_b11m_active);
    w.b("b11m_shadow", m_pre_b11m_shadow);
}

emulator::Result AZBK::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.array("windows", m_win, 16);
    for (unsigned int &v : m_win) v &= 017777;
    r.u("active", m_active);
    r.u("readonly", m_ro);
    r.u("shadow", m_shadow);
    r.u("control", m_ctrl);
    r.u("copy130", m_copy130);
    r.u("copy716", m_copy716);
    r.u("last716", m_last716);
    r.b("start", m_start);
    r.b("smk_flag", m_smk_flag);
    r.array("smk_windows", m_pre_smk_win, 8);
    r.u("smk_active", m_pre_smk_active);
    r.u("smk_readonly", m_pre_smk_ro);
    r.array("b11m_windows", m_pre_b11m_win, 4);
    r.b("b11m_active", m_pre_b11m_active);
    r.b("b11m_shadow", m_pre_b11m_shadow);
    // Ответы route() другие: диспетчер разберет страницы заново, экран -
    // перерисует все
    touch_all();
    for (uint32_t &k : m_route_key) k = ~0u;
    routes_changed();
    return emulator::Result::ok();
}

//----------------------------------------------------------------- поля ---

std::vector<DeviceFieldInfo> AZBK::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"windows",  "Each 4 KB window: the AZ page and A (answers), R (read only), S (shadow)", false});
    r.push_back({"pages",    "AZ page of the windows 0-15 (0177300-0177336)",          false});
    r.push_back({"active",   "0177340: windows that answer",                            false});
    r.push_back({"readonly", "0177342: read-only windows",                              false});
    r.push_back({"shadow",   "0177344: shadow windows",                                 false});
    r.push_back({"control",  "0177346: mapper control",                                 false});
    r.push_back({"start",    "1 until the processor's start read of 0177716 (start at 0170000)", false});
    r.push_back({"last_716", "0177352: last memory write to 0177716 taken by the mapper", false});
    r.push_back({"last_130", "0177350: last mode written to 0177130 (СМК)",              false});
    r.push_back({"smk_flag", "1 after 6 was written to 0177130",                        false});
    return r;
}

bool AZBK::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    auto number = [&out](unsigned int v, unsigned int width) {
        out.numeric = true;
        out.width = width;
        out.values.push_back(v);
        return true;
    };
    if (field == "pages") {
        out.numeric = true;
        out.width = 16;
        for (unsigned int n = 0; n < 16; n++) out.values.push_back(m_win[n]);
        return true;
    }
    if (field == "active")   return number(m_active, 16);
    if (field == "readonly") return number(m_ro, 16);
    if (field == "shadow")   return number(m_shadow, 16);
    if (field == "control")  return number(m_ctrl, 16);
    if (field == "start")    return number(m_start ? 1 : 0, 1);
    if (field == "last_716") return number(m_copy716, 16);
    if (field == "last_130") return number(m_copy130, 16);
    if (field == "smk_flag") return number(m_smk_flag ? 1 : 0, 1);
    if (field == "windows") {
        out.numeric = false;
        for (unsigned int n = 0; n < 16; n++) {
            std::string flags;
            flags += ((m_active >> n) & 1) ? 'A' : '-';
            flags += window_ro(n) ? 'R' : '-';
            flags += ((m_shadow >> n) & 1) ? 'S' : '-';
            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + oct_str(n << 12, 6) + ": " + oct_str(m_win[n], 5) + " " + flags;
        }
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_azbk(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBK(im, cd);
}
