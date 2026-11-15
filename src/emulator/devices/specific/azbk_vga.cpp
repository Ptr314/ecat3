// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): VGA timebase, video registers, frame timer

#include <algorithm>
#include <climits>
#include <cstring>

#include "azbk_vga.h"
#include "azbk.h"
#include "azbk_display.h"
#include "emulator/utils.h"

// Регистры, абсолютные адреса
static const unsigned int R_MODE   = 0177230;
static const unsigned int R_PAGE0  = 0177232;
static const unsigned int R_PALIDX = 0177234;
static const unsigned int R_PALVAL = 0177236;
static const unsigned int R_PAGE1  = 0177240;
static const unsigned int R_PAGE2  = 0177242;
static const unsigned int R_VSCR2  = 0177244;
static const unsigned int R_VSCR1  = 0177246;
static const unsigned int R_VSCR0  = 0177250;
static const unsigned int R_HSCR0  = 0177252;
static const unsigned int R_HSCR1  = 0177254;
static const unsigned int R_HSCR2  = 0177256;
static const unsigned int R_BLTCTL = 0177270;
static const unsigned int R_BLTPG  = 0177272;
static const unsigned int R_LFSR   = 0177550;
static const unsigned int R_BKPAL  = 0177662;
static const unsigned int R_BKSCR  = 0177664;

// Стандартная палитра - таблица PalData стартового ПЗУ MAXIOL (AZLIB02, trap 30
// «восстановление палитры»): 256 цветов (серые, затем web-safe), 16 палитр
// по 4 цвета БК11М, 16 цветов, 2 цвета. При включении ее же ставим и сюда:
// что в ячейках до первого trap 30, не описано
static const uint16_t PAL_DATA[AZBKVga::PALETTE_SIZE] = {
    0000000, 0002041, 0004102, 0006143, 0010204, 0012245, 0014306, 0016347,
    0020410, 0022451, 0024512, 0026553, 0030614, 0032655, 0034716, 0036757,
    0041020, 0043061, 0045122, 0047163, 0051224, 0053265, 0055326, 0057367,
    0061430, 0063471, 0065532, 0067573, 0071634, 0073675, 0075736, 0077777,
    0000000, 0000037, 0001740, 0001777, 0076000, 0076037, 0077740, 0077777,
    0000000, 0000006, 0000014, 0000023, 0000031, 0000037, 0000300, 0000306,
    0000314, 0000323, 0000331, 0000337, 0000600, 0000606, 0000614, 0000623,
    0000631, 0000637, 0001140, 0001146, 0001154, 0001163, 0001171, 0001177,
    0001440, 0001446, 0001454, 0001463, 0001471, 0001477, 0001740, 0001746,
    0001754, 0001763, 0001771, 0001777, 0014000, 0014006, 0014014, 0014023,
    0014031, 0014037, 0014300, 0014306, 0014314, 0014323, 0014331, 0014337,
    0014600, 0014606, 0014614, 0014623, 0014631, 0014637, 0015140, 0015146,
    0015154, 0015163, 0015171, 0015177, 0015440, 0015446, 0015454, 0015463,
    0015471, 0015477, 0015740, 0015746, 0015754, 0015763, 0015771, 0015777,
    0030000, 0030006, 0030014, 0030023, 0030031, 0030037, 0030300, 0030306,
    0030314, 0030323, 0030331, 0030337, 0030600, 0030606, 0030614, 0030623,
    0030631, 0030637, 0031140, 0031146, 0031154, 0031163, 0031171, 0031177,
    0031440, 0031446, 0031454, 0031463, 0031471, 0031477, 0031740, 0031746,
    0031754, 0031763, 0031771, 0031777, 0046000, 0046006, 0046014, 0046023,
    0046031, 0046037, 0046300, 0046306, 0046314, 0046323, 0046331, 0046337,
    0046600, 0046606, 0046614, 0046623, 0046631, 0046637, 0047140, 0047146,
    0047154, 0047163, 0047171, 0047177, 0047440, 0047446, 0047454, 0047463,
    0047471, 0047477, 0047740, 0047746, 0047754, 0047763, 0047771, 0047777,
    0062000, 0062006, 0062014, 0062023, 0062031, 0062037, 0062300, 0062306,
    0062314, 0062323, 0062331, 0062337, 0062600, 0062606, 0062614, 0062623,
    0062631, 0062637, 0063140, 0063146, 0063154, 0063163, 0063171, 0063177,
    0063440, 0063446, 0063454, 0063463, 0063471, 0063477, 0063740, 0063746,
    0063754, 0063763, 0063771, 0063777, 0076000, 0076006, 0076014, 0076023,
    0076031, 0076037, 0076300, 0076306, 0076314, 0076323, 0076331, 0076337,
    0076600, 0076606, 0076614, 0076623, 0076631, 0076637, 0077140, 0077146,
    0077154, 0077163, 0077171, 0077177, 0077440, 0077446, 0077454, 0077463,
    0077471, 0077477, 0077740, 0077746, 0077754, 0077763, 0077771, 0077777,
    0000000, 0000037, 0001740, 0076000, 0000000, 0077740, 0076037, 0076000,
    0000000, 0001777, 0000037, 0076037, 0000000, 0001740, 0001777, 0077740,
    0000000, 0076037, 0001777, 0077777, 0000000, 0077777, 0077777, 0077777,
    0000000, 0056000, 0046000, 0076000, 0000000, 0057740, 0047740, 0077740,
    0000000, 0056037, 0046037, 0076037, 0000000, 0047740, 0046037, 0046000,
    0000000, 0057740, 0056037, 0056000, 0000000, 0001777, 0077740, 0076000,
    0000000, 0076000, 0001740, 0001777, 0000000, 0001777, 0077740, 0077777,
    0000000, 0077740, 0001740, 0077777, 0000000, 0001777, 0001740, 0077777,
    0000000, 0000025, 0001240, 0001265, 0052000, 0052025, 0052500, 0053265,
    0024512, 0000037, 0001740, 0001777, 0076000, 0076037, 0077740, 0077777,
    0000020, 0077777,
};

const unsigned int AZBKVga::ROLL_WORDS[12] = {
    6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608, 262144
};

AZBKVga::AZBKVga(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd),
    i_irq(this, im, 1, "irq", MODE_W)
{
    addresable_size = 0200000;
    can_read = true;
    can_write = true;
    m_clocked = true;
}

void AZBKVga::default_palette(uint16_t * palette)
{
    memcpy(palette, PAL_DATA, sizeof(PAL_DATA));
}

emulator::Result AZBKVga::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_board = dynamic_cast<AZBK*>(im->dm->get_device_by_name(cd->get_parameter("board").value));
    if (m_board == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);
    m_blit.set_board(m_board);
    m_blit.write_cost = read_confg_value(cd, "blit_write", false, m_blit.write_cost);
    m_blit.read_cost = read_confg_value(cd, "blit_read", false, m_blit.read_cost);
    m_dot_freq = read_confg_value(cd, "frequency", false, 65000000u);
    if (m_dot_freq == 0) m_dot_freq = 65000000;
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    return emulator::Result::ok();
}

void AZBKVga::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (!cold) return;      // видеоконтроллер сброса БК не видит
    m_acc = 0;
    m_ticks = 0;
    m_dot = 0;
    m_div48 = 0;
    m_irq_left = 0;
    m_frames = m_irqs = 0;
    i_irq.change(0);
    // Как у стартового ПЗУ до настройки: 512x256 ч/б, страница 4
    m_mode = 012100;
    for (unsigned int i = 0; i < 3; i++) {
        m_page[i] = 4;
        m_vscroll[i] = 0;
        m_hscroll[i] = 0;
    }
    m_pal_index = 0;
    default_palette(m_palette);
    m_legacy_pal = 0;
    m_extended = false;
    m_blit.reset();
    m_blit.write_page(0);
    m_halts_seen = m_board != nullptr ? m_board->halts() : 0;
    m_lfsr[0] = 0x2545F491;
    m_lfsr[1] = 0x9E3779B9;
    m_lfsr[2] = 0x85EBCA6B;
    m_lfsr[3] = 0xC2B2AE35;
    m_palette_version++;
    latch_frame();
}

void AZBKVga::latch_frame()
{
    m_f_mode = m_mode;
    for (unsigned int i = 0; i < 3; i++) {
        m_f_vscroll[i] = m_vscroll[i];
        m_f_pages[i] = m_page[i];
        m_f_hscroll[i] = m_hscroll[i];
    }
    m_f_quarter = m_extended;
}

// Разряд 11 режима: страницы и горизонтальные скроллы меняются только с
// новым кадром; без него - со следующей строки (MAXIOL, тема 5556)
void AZBKVga::line_state(AZBKLineState &s) const
{
    s.mode = m_f_mode;
    const bool sync = (m_f_mode & 04000) != 0;
    for (unsigned int i = 0; i < 3; i++) {
        s.vscroll[i] = m_f_vscroll[i];
        s.pages[i] = sync ? m_f_pages[i] : m_page[i];
        s.hscroll[i] = sync ? m_f_hscroll[i] : m_hscroll[i];
    }
    s.legacy_pal = m_legacy_pal;
    s.quarter = m_f_quarter;
}

//------------------------------------------------------------- развертка ---

void AZBKVga::raise_irq()
{
    m_irqs++;
    m_irq_left = DOTS_PER_LINE;
    i_irq.change(1);
}

// Начало строки 769: кадр показан. Таймер 60 Гц - разряд 3 177346 при разряде
// 2 = 0
void AZBKVga::frame_event()
{
    m_frames++;
    m_blit.frame_end();
    const unsigned int ctrl = m_board->control();
    if ((ctrl & AZBK::CTRL_V100) != 0 && (ctrl & AZBK::CTRL_50HZ) == 0) raise_irq();
}

// Начало строки: строку 0 кадр защелкивает, видимые строки рисует экран (буфер
// строки ПЛИС набирает на предыдущей), начало строки 768 - картина готова
void AZBKVga::line_event(unsigned int line)
{
    if (line == 0) latch_frame();
    if (m_display == nullptr) return;
    if (line < VISIBLE_LINES) m_display->draw_line(line);
    else if (line == VISIBLE_LINES) m_display->frame_done();
}

// Точек до ближайшего события: снятия запроса, строки 769 или такта 48.8 Гц
unsigned int AZBKVga::dots_to_event(bool lines) const
{
    unsigned int d = UINT_MAX;
    if (lines) d = DOTS_PER_LINE - m_dot % DOTS_PER_LINE;
    if (m_irq_left > 0) d = std::min(d, m_irq_left);
    const unsigned int irq_dot = IRQ_LINE * DOTS_PER_LINE;
    const unsigned int to_frame = (m_dot < irq_dot) ? irq_dot - m_dot : DOTS_PER_FRAME - m_dot + irq_dot;
    d = std::min(d, to_frame);
    d = std::min(d, DIV48 - m_div48);
    return d;
}

void AZBKVga::advance(unsigned int dots)
{
    while (dots > 0) {
        const unsigned int step = std::min(dots, dots_to_event(true));
        dots -= step;
        if (m_blit.busy() && !m_blit_hold) m_blit.run(step);
        m_ticks += step;
        m_dot += step;
        if (m_dot >= DOTS_PER_FRAME) m_dot -= DOTS_PER_FRAME;
        m_div48 += step;
        if (m_irq_left > 0) {
            m_irq_left -= step;
            if (m_irq_left == 0) i_irq.change(0);
        }
        if (m_dot % DOTS_PER_LINE == 0) line_event(m_dot / DOTS_PER_LINE);
        if (m_dot == IRQ_LINE * DOTS_PER_LINE) frame_event();
        if (m_div48 >= DIV48) {
            m_div48 = 0;
            const unsigned int ctrl = m_board->control();
            if ((ctrl & AZBK::CTRL_V100) != 0 && (ctrl & AZBK::CTRL_50HZ) != 0) raise_irq();
        }
    }
}

void AZBKVga::clock(unsigned int counter)
{
    if (m_system_clock == 0) return;
    // Низкий HALT (команда 037) сбрасывает регистр блиттера
    if (m_board->halts() != m_halts_seen) {
        m_halts_seen = m_board->halts();
        m_blit.reset();
    }
    // Пуск записью в 177270 - последний цикл шины этой команды: такты до него
    // блиттеру не идут (иначе короткий пакет кончался бы раньше, чем
    // программа MAXIOL успевает увидеть разряд занятости)
    m_blit_hold = m_blit.busy() && !m_blit_was_busy;
    m_acc += (uint64_t)counter * m_dot_freq;
    const uint64_t dots = m_acc / m_system_clock;
    m_acc -= dots * m_system_clock;
    if (dots > 0) advance((unsigned int)dots);
    m_blit_hold = false;
    m_blit_was_busy = m_blit.busy();
}

// Такты процессора до события, с округлением вверх: прерывание не должно
// прийти позже, чем на настоящей плате
unsigned int AZBKVga::clocks_to_event() const
{
    if (m_system_clock == 0) return UINT_MAX;
    const unsigned int ctrl = m_board->control();
    if (m_irq_left == 0 && (ctrl & AZBK::CTRL_V100) == 0) return UINT_MAX;
    const uint64_t need = (uint64_t)dots_to_event(false) * m_system_clock;
    if (need <= m_acc) return 1;
    const uint64_t clocks = (need - m_acc + m_dot_freq - 1) / m_dot_freq;
    return (clocks > UINT_MAX) ? UINT_MAX : (unsigned int)clocks;
}

//------------------------------------------------------------- регистры ---

unsigned int AZBKVga::read_register(unsigned int address, bool direct)
{
    switch (address & ~1u) {
    case R_MODE:   return m_mode;
    case R_PAGE0:  return m_page[0];
    case R_PALIDX: return m_pal_index;
    case R_PALVAL: return m_palette[m_pal_index < PALETTE_SIZE ? m_pal_index : 0];
    case R_PAGE1:  return m_page[1];
    case R_PAGE2:  return m_page[2];
    case R_VSCR2:  return m_vscroll[2];
    case R_VSCR1:  return m_vscroll[1];
    case R_VSCR0:  return m_vscroll[0];
    case R_HSCR0:  return m_hscroll[0];
    case R_HSCR1:  return m_hscroll[1];
    case R_HSCR2:  return m_hscroll[2];
    case R_BLTCTL: return m_blit.read_control();
    case R_BLTPG:  return m_blit.page();
    case R_LFSR: {
        if (direct) return (m_lfsr[0] ^ (uint32_t)m_ticks) & 0177777;
        // xorshift128 + время: каждое чтение - новое слово
        uint32_t t = m_lfsr[3];
        const uint32_t s = m_lfsr[0];
        m_lfsr[3] = m_lfsr[2];
        m_lfsr[2] = m_lfsr[1];
        m_lfsr[1] = s;
        t ^= t << 11;
        t ^= t >> 8;
        m_lfsr[0] = t ^ s ^ (s >> 19);
        return (m_lfsr[0] ^ (uint32_t)(m_ticks * 0x9E3779B9u)) & 0177777;
    }
    default:       return _FFFF;
    }
}

void AZBKVga::write_register(unsigned int address, unsigned int value, bool word)
{
    const unsigned int a = address & ~1u;
    value &= 0177777;
    switch (a) {
    case R_MODE:   m_mode = value; break;
    case R_PAGE0:  m_page[0] = value & 017777; break;
    case R_PALIDX: m_pal_index = value & 0777; break;
    case R_PALVAL:
        if (m_pal_index < PALETTE_SIZE) {
            m_palette[m_pal_index] = (uint16_t)(value & 077777);
            m_palette_version++;
        }
        break;
    case R_PAGE1:  m_page[1] = value & 017777; break;
    case R_PAGE2:  m_page[2] = value & 017777; break;
    case R_VSCR2:  m_vscroll[2] = value & 03777; break;
    case R_VSCR1:  m_vscroll[1] = value & 03777; break;
    case R_VSCR0:  m_vscroll[0] = value & 03777; break;
    case R_HSCR0:  m_hscroll[0] = value & 0377; break;
    case R_HSCR1:  m_hscroll[1] = value & 0377; break;
    case R_HSCR2:  m_hscroll[2] = value & 0377; break;
    case R_BLTCTL: m_blit.write_control(value); break;
    case R_BLTPG:  m_blit.write_page(value); break;
    case R_BKPAL:
        // Старший байт 177662 БК: палитра и экранная страница. Младший байт
        // (запись байта в 177662) их не трогает
        if (!word && (address & 1) == 0) break;
        if (!word) value <<= 8;
        m_legacy_pal = (value >> 8) & 017;
        m_page[0] = (value & 0100000) ? 034 : 4;
        break;
    case R_BKSCR:
        // 177664: скролл в младшем байте (0330 - без сдвига), бит 9 -
        // полный экран; запись байта меняет только свою половину
        if (word || (address & 1) == 0) m_vscroll[0] = ((value & 0377) - 0330) & 03777;
        if (word || (address & 1) != 0) m_extended = ((word ? value : value << 8) & 01000) == 0;
        break;
    default:
        break;
    }
}

unsigned int AZBKVga::get_value(unsigned int address)
{
    const unsigned int v = read_register(address, false);
    if (v == _FFFF) return v;
    return (address & 1) ? (v >> 8) & 0xFF : v & 0xFF;
}

unsigned int AZBKVga::get_direct(unsigned int address)
{
    const unsigned int v = read_register(address, true);
    if (v == _FFFF) return v;
    return (address & 1) ? (v >> 8) & 0xFF : v & 0xFF;
}

unsigned int AZBKVga::get_value_word(unsigned int address)
{
    return read_register(address, false);
}

void AZBKVga::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    const unsigned int a = address & ~1u;
    if (a == R_BKPAL || a == R_BKSCR) {
        write_register(address, value & 0xFF, false);
        return;
    }
    const unsigned int old = read_register(a, true);
    if (old == _FFFF) return;
    value &= 0xFF;
    write_register(a, (address & 1) ? ((old & 0xFF) | (value << 8)) : ((old & 0xFF00) | value), true);
}

void AZBKVga::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    write_register(address, value, true);
}

//---------------------------------------------------------------- снимок ---

void AZBKVga::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n64("acc", m_acc);
    w.n64("ticks", m_ticks);
    w.n("dot", m_dot);
    w.n("div48", m_div48);
    w.n("irq_left", m_irq_left);
    w.n64("frames", m_frames);
    w.n64("irqs", m_irqs);
    w.u("mode", m_mode, 16);
    w.array("pages", m_page, 3);
    w.array("vscroll", m_vscroll, 3);
    w.array("hscroll", m_hscroll, 3);
    w.u("palette_index", m_pal_index, 16);
    w.array("palette", m_palette, PALETTE_SIZE);
    w.u("legacy_palette", m_legacy_pal, 8);
    w.b("extended", m_extended);
    m_blit.save_state(w);
    w.n("halts_seen", m_halts_seen);
    w.array("lfsr", m_lfsr, 4);
    w.u("frame_mode", m_f_mode, 16);
    w.array("frame_pages", m_f_pages, 3);
    w.array("frame_vscroll", m_f_vscroll, 3);
    w.array("frame_hscroll", m_f_hscroll, 3);
    w.b("frame_quarter", m_f_quarter);
}

emulator::Result AZBKVga::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.n64("acc", m_acc);
    r.n64("ticks", m_ticks);
    r.u("dot", m_dot);
    r.u("div48", m_div48);
    r.u("irq_left", m_irq_left);
    r.n64("frames", m_frames);
    r.n64("irqs", m_irqs);
    r.u("mode", m_mode);
    r.array("pages", m_page, 3);
    r.array("vscroll", m_vscroll, 3);
    r.array("hscroll", m_hscroll, 3);
    r.u("palette_index", m_pal_index);
    r.array("palette", m_palette, PALETTE_SIZE);
    r.u("legacy_palette", m_legacy_pal);
    r.b("extended", m_extended);
    m_blit.load_state(r);
    r.u("halts_seen", m_halts_seen);
    r.array("lfsr", m_lfsr, 4);
    if (m_dot >= DOTS_PER_FRAME) m_dot = 0;
    if (m_div48 >= DIV48) m_div48 = 0;
    r.u("frame_mode", m_f_mode);
    r.array("frame_pages", m_f_pages, 3);
    r.array("frame_vscroll", m_f_vscroll, 3);
    r.array("frame_hscroll", m_f_hscroll, 3);
    r.b("frame_quarter", m_f_quarter);
    m_palette_version++;
    return emulator::Result::ok();
}

//----------------------------------------------------------------- поля ---

std::vector<DeviceFieldInfo> AZBKVga::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"mode",     "0177230: video mode",                                  false});
    r.push_back({"pages",    "AZ page of the layers 0 (top), 1, 2",                  false});
    r.push_back({"vscroll",  "Vertical scroll of the layers 0, 1, 2, lines of the roll", false});
    r.push_back({"hscroll",  "Horizontal scroll of the layers 0, 1, 2, words",       false});
    r.push_back({"palette",  "Palette cells from-to (R5G5B5)",                       true});
    r.push_back({"legacy_palette", "4-colour palette from 0177662",                  false});
    r.push_back({"line",     "Raster line the beam is on, 0-805",                    false});
    r.push_back({"frames",   "VGA frames since power-on",                            false});
    r.push_back({"irqs",     "Frame interrupts raised",                              false});
    r.push_back({"blit_busy",  "1 while the blitter runs (bit 15 of 0177270)",       false});
    r.push_back({"blit_words", "Words the blitter processed since power-on",         false});
    r.push_back({"blit_ticks", "Dots of 65 MHz the blitter spent running",           false});
    return r;
}

bool AZBKVga::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "mode")    { out.width = 16; out.values.push_back(m_mode); return true; }
    if (field == "pages")   { out.width = 16; for (unsigned int v : m_page) out.values.push_back(v); return true; }
    if (field == "vscroll") { out.width = 16; for (unsigned int v : m_vscroll) out.values.push_back(v); return true; }
    if (field == "hscroll") { out.width = 16; for (unsigned int v : m_hscroll) out.values.push_back(v); return true; }
    if (field == "legacy_palette") { out.values.push_back(m_legacy_pal); return true; }
    if (field == "line")    { out.values.push_back(m_dot / DOTS_PER_LINE); return true; }
    if (field == "frames")  { out.values.push_back((unsigned int)m_frames); return true; }
    if (field == "irqs")    { out.values.push_back((unsigned int)m_irqs); return true; }
    if (field == "blit_busy")  { out.values.push_back(m_blit.busy() ? 1 : 0); return true; }
    if (field == "blit_words") { out.width = 32; out.values.push_back((unsigned int)m_blit.words()); return true; }
    if (field == "blit_ticks") { out.width = 32; out.values.push_back((unsigned int)m_blit.ticks()); return true; }
    if (field == "palette") {
        out.width = 16;
        if (to < from) to = from;
        for (unsigned int i = from; i <= to && i < PALETTE_SIZE; i++) out.values.push_back(m_palette[i]);
        return true;
    }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_azbk_vga(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new AZBKVga(im, cd);
}
