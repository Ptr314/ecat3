// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Видеоконтроллер НВ1 и расширитель НВ2 Электроники МС 0585

#include <cstring>

#include "e85_video.h"
#include "emulator/utils.h"
#include "emulator/renderer.h"

#define CALLBACK_INIT   1

#define REG_ID      000
#define REG_CSR     004
#define REG_RUD1    006
#define REG_RUD2    010
#define REG_RUC     012
#define REG_SCROLL  014
#define REG_X       016
#define REG_Y       020
#define REG_COUNT   022
#define REG_PATTERN 024
#define REG_BASE    026

#define EXPANSION_REGS  0400    // база регистров НВ2 в карте памяти
#define MEMORY_START    01000   // выше - окно памяти

#define CSR_READY       0100000
#define CSR_VECTOR_IE   0040000
#define CSR_NO_EXPANSION 0020000
#define CSR_FRAME_END   0000200
#define CSR_FRAME_IE    0000100
#define CSR_EVEN        0000040
#define CSR_WRITABLE    0043503

#define RUD_ENABLE      0040
#define RUD_MASK        0077
#define RUD2_MASK       0037477
#define RUD_RESOLUTION  0030
#define RUD2_RESOLUTION 014030

// Что отвечают регистры только для записи, ТО НВ1 не говорит. 0177577 взято из Xhomer:
// там так отвечают РДВ, РФЯ и РФА с пометкой, что так делает настоящий DEC Pro. На НВ1
// это не проверено; РУЦ у него 0177777, здесь он в той же группе. ПЗУ их не читает
#define WRITE_ONLY_READ 0177577
#define ID_HIGH         0177400

#define WINDOW_BYTES    0100000u

// Время построения, такты ЦП 6,67 МГц. ТО его не даёт, а 800 нс в нём - шаг адреса при
// развёртке, по 16 точек на слово: это оценка для CLOCKS_PER_WORD. CLOCKS_PER_BIT - оценка
// без расчёта. Граница одна: тест ПЗУ 140650 ждёт 43690 точек не дольше 5421 прохода
// цикла опроса, это около 80 мс на F-11 этой модели, или 12 тактов на точку
#define CLOCKS_PER_BIT  2
#define CLOCKS_PER_WORD 5

#define FRAME_LINES_625 625
#define FRAME_LINES_525 525
#define VISIBLE_625     256
#define VISIBLE_525     240
#define FULL_EVERY      50

//------------------------------- E85Video ------------------------------------

E85Video::E85Video(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
{
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = 1u << 22;
    for (unsigned int i = 0; i < 3; i++) m_planes[i].assign(WORDS, 0);
}

emulator::Result E85Video::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_expansion = read_confg_value(cd, "expansion", false, false);
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));
    return emulator::Result::ok();
}

// Начальная установка: тест ПЗУ проверяет регистры сразу после команды RESET
void E85Video::clear_registers()
{
    m_csr = CSR_READY;
    m_rud1 = 0;
    m_rud2 = 0;
    m_scroll = 0;
    m_busy = 0;
    if (m_mapper != nullptr) m_mapper->routing_changed();
    if (m_display != nullptr) m_display->all_changed();
    update_irqb();
}

void E85Video::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        for (unsigned int i = 0; i < 3; i++) m_planes[i].assign(WORDS, 0);
        memset(m_ruc, 0, sizeof(m_ruc));
    }
    m_x = 0;
    m_y = 0;
    m_pattern = 0;
    m_base = 0;
    i_irqa.change(0);
    clear_registers();
}

void E85Video::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT && (new_value & 1) != 0) clear_registers();
}

// Уровень: запрос встаёт и при разрешении над уже готовым вектором, на этом
// стоит тест ПЗУ (140476)
void E85Video::update_irqb()
{
    const unsigned int line = ((m_csr & CSR_READY) != 0 && (m_csr & CSR_VECTOR_IE) != 0) ? 1 : 0;
    if (i_irqb.value != line) i_irqb.change(line);
}

void E85Video::clock(unsigned int counter)
{
    if (m_busy == 0) return;
    if (m_busy > counter) {
        m_busy -= counter;
        return;
    }
    m_busy = 0;
    m_csr |= CSR_READY;
    update_irqb();
}

void E85Video::frame_end()
{
    m_frames++;
    m_vblank = true;
    if (m_csr & CSR_FRAME_IE) {
        i_irqa.change(1);
        i_irqa.change(0);
    }
}

void E85Video::frame_start()
{
    m_vblank = false;
    m_even = !m_even;
}

unsigned int E85Video::plane_enables() const
{
    unsigned int v = (m_rud1 & RUD_ENABLE) ? 1 : 0;
    if (m_expansion) {
        if (m_rud2 & RUD_ENABLE) v |= 2;
        if (m_rud2 & (RUD_ENABLE << 8)) v |= 4;
    }
    return v;
}

// РУД пишут перед каждой операцией с вектором. Картину меняет только разрешение
// страницы, а ответ окна - только то, разрешена ли хоть одна
void E85Video::set_rud(unsigned int &rud, unsigned int value, unsigned int resolution)
{
    const bool window = plane_enables() != 0;
    const unsigned int changed = rud ^ value;
    rud = value;
    if ((plane_enables() != 0) != window && m_mapper != nullptr) m_mapper->routing_changed();
    if ((changed & resolution) != 0 && m_display != nullptr) m_display->all_changed();
}

// Окно отвечает, только пока хотя бы одна страница разрешена. Всё остальное -
// пустота: ПЗУ проверяет это по прерыванию через 4
unsigned int E85Video::route(unsigned int address, MAYBE_UNUSED unsigned int mode)
{
    if ((address >> 15) == m_base && plane_enables() != 0) return ROUTE_ANSWER;
    return ROUTE_PASS;
}

unsigned int E85Video::memory_index(unsigned int address) const
{
    return (((address & (WINDOW_BYTES - 1)) >> 1) + (m_scroll << 6)) & (WORDS - 1);
}

unsigned int E85Video::read_register(unsigned int offset) const
{
    switch (offset) {
    case REG_ID:
        return ID_HIGH | 002;
    case REG_ID + EXPANSION_REGS:
        return ID_HIGH | 003;
    case REG_CSR: {
        unsigned int v = m_csr;
        if (!m_expansion) v |= CSR_NO_EXPANSION;
        if (m_vblank) v |= CSR_FRAME_END;
        if (m_even) v |= CSR_EVEN;
        return v;
    }
    case REG_RUD1:   return m_rud1;
    case REG_RUD2:   return m_rud2;
    case REG_SCROLL: return ID_HIGH | m_scroll;
    case REG_X:      return m_x;
    case REG_Y:      return m_y;
    case REG_RUC:
    case REG_COUNT:
    case REG_PATTERN:
    case REG_BASE:
        return WRITE_ONLY_READ;
    default:
        return 0;
    }
}

void E85Video::write_register(unsigned int offset, unsigned int value)
{
    switch (offset) {
    case REG_CSR: {
        const unsigned int old = m_csr;
        m_csr = (m_csr & ~(unsigned int)CSR_WRITABLE) | (value & CSR_WRITABLE);
        update_irqb();
        if (((old ^ m_csr) & 02001) != 0 && m_display != nullptr) m_display->all_changed();
        break;
    }
    case REG_RUD1:
        set_rud(m_rud1, value & RUD_MASK, RUD_RESOLUTION);
        break;
    case REG_RUD2:
        set_rud(m_rud2, value & RUD2_MASK, RUD2_RESOLUTION);
        break;
    case REG_RUC: {
        const unsigned int index = (value >> 8) & 7;
        const unsigned int entry = value & 0xFF;
        if (m_ruc[index] == entry) break;
        m_ruc[index] = entry;
        if (color_map() && m_display != nullptr) m_display->all_changed();
        break;
    }
    case REG_SCROLL:
        if ((value & 0xFF) == m_scroll) break;
        m_scroll = value & 0xFF;
        if (m_display != nullptr) m_display->all_changed();
        break;
    case REG_X:
        m_x = value & 01777;
        break;
    case REG_Y:
        m_y = value & 0377;
        break;
    case REG_COUNT:
        run_vector(value & 0xFFFF);
        break;
    case REG_PATTERN:
        m_pattern = value & 0xFFFF;
        break;
    case REG_BASE:
        m_base = value & 0177;
        if (m_mapper != nullptr) m_mapper->routing_changed();
        break;
    default:
        break;
    }
}

// Операция над страницей идёт вся разом, готовность встаёт через время построения
void E85Video::run_vector(unsigned int count)
{
    const bool words = (m_csr & 01000) != 0;
    m_vectors++;
    m_csr &= ~(unsigned int)CSR_READY;
    update_irqb();

    const unsigned int ops[3] = { m_rud1 & 7, m_rud2 & 7, (m_rud2 >> 8) & 7 };
    const unsigned int planes = m_expansion ? 3 : 1;
    for (unsigned int p = 0; p < planes; p++) {
        if (words) word_vector(p, ops[p], count);
        else bit_vector(p, ops[p], count);
    }

    // РФЯ - циклический регистр: за вектор он провернулся на число точек
    if (!words) {
        const unsigned int n = count & 15;
        if (n != 0) m_pattern = ((m_pattern >> n) | (m_pattern << (16 - n))) & 0xFFFF;
    }
    m_busy = (count == 0) ? 1 : count * (words ? CLOCKS_PER_WORD : CLOCKS_PER_BIT);
}

void E85Video::bit_vector(unsigned int plane, unsigned int op, unsigned int count)
{
    if (op == 0) return;
    const bool down = (m_csr & 0400) != 0;
    uint16_t * mem = m_planes[plane].data();
    unsigned int x = m_x;
    unsigned int y = m_y;
    unsigned int pattern = m_pattern;

    for (unsigned int i = 0; i < count; i++) {
        const unsigned int physical = (y + m_scroll) & (LINES - 1);
        uint16_t & cell = mem[(physical << 6) | ((x >> 4) & 077)];
        const unsigned int mask = 1u << (x & 15);
        const bool bit = (pattern & 1) != 0;
        switch (op) {
        case 1: if (bit) cell ^= (uint16_t)mask; break;
        case 2: if (bit) cell |= (uint16_t)mask; else cell &= (uint16_t)~mask; break;
        case 3: if (bit) cell &= (uint16_t)~mask; else cell |= (uint16_t)mask; break;
        case 4: if (bit) cell |= (uint16_t)mask; break;
        case 5: if (bit) cell &= (uint16_t)~mask; break;
        case 6: cell &= (uint16_t)~mask; break;
        default: cell |= (uint16_t)mask; break;
        }
        if (m_display != nullptr) m_display->line_changed(physical);
        pattern = (pattern >> 1) | ((pattern & 1) << 15);

        if (down) {
            if (++y == LINES) {
                y = 0;
                x = (x + 1) & 01777;
            }
        } else if (++x == LINES * 4) {
            x = 0;
            y = (y + 1) & (LINES - 1);
        }
    }
}

// Сдвиг изображения: разряды, ушедшие за край слова, входят в следующее,
// а в первое - младший разряд РФЯ на все шестнадцать
void E85Video::word_vector(unsigned int plane, unsigned int op, unsigned int count)
{
    if (op == 0 || op == 4) return;
    const bool backward = (m_csr & 0400) != 0;
    uint16_t * mem = m_planes[plane].data();
    const unsigned int shift = (op == 5) ? 1 : ((op == 6) ? 2 : 4);
    const uint16_t fill = (m_pattern & 1) ? 0xFFFF : 0;
    unsigned int index = (((m_y + m_scroll) & (LINES - 1)) << 6) | ((m_x >> 4) & 077);
    uint16_t carry = fill;

    for (unsigned int i = 0; i < count; i++) {
        const uint16_t old = mem[index];
        switch (op) {
        case 1: mem[index] = (uint16_t)~old; break;
        case 2: mem[index] = fill; break;
        case 3: mem[index] = (uint16_t)~fill; break;
        default:
            if (backward) mem[index] = (uint16_t)((old >> shift) | (carry << (16 - shift)));
            else mem[index] = (uint16_t)((old << shift) | (carry >> (16 - shift)));
            carry = old;
            break;
        }
        if (m_display != nullptr) m_display->line_changed(index >> 6);
        index = (index + (backward ? WORDS - 1 : 1)) & (WORDS - 1);
    }
}

unsigned int E85Video::get_value_word(unsigned int address)
{
    if (address < MEMORY_START) return read_register(address & 0776);
    const unsigned int index = memory_index(address);
    const unsigned int enables = plane_enables();
    return m_planes[(enables & 1) ? 0 : ((enables & 2) ? 1 : 2)][index];
}

void E85Video::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (address < MEMORY_START) {
        write_register(address & 0776, value);
        return;
    }
    const unsigned int index = memory_index(address);
    const unsigned int enables = plane_enables();
    for (unsigned int p = 0; p < 3; p++)
        if (enables & (1u << p)) m_planes[p][index] = (uint16_t)value;
    if (m_display != nullptr) m_display->line_changed(index >> 6);
}

unsigned int E85Video::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void E85Video::set_value(unsigned int address, unsigned int value, bool force)
{
    value &= 0xFF;
    const unsigned int w = get_value_word(address & ~1u);
    set_value_word(address & ~1u, (address & 1) ? ((w & 0xFF) | (value << 8)) : ((w & 0xFF00) | value), force);
}

std::vector<DeviceFieldInfo> E85Video::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"csr",     "Command and status register", false});
    r.push_back({"rud",     "Data control registers 1 and 2", false});
    r.push_back({"scroll",  "Vertical shift", false});
    r.push_back({"base",    "Base of the memory window, address bits 21-15", false});
    r.push_back({"frames",  "Frames since the start", false});
    r.push_back({"vectors", "Vectors built since the start", false});
    return r;
}

bool E85Video::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 16;
    if (field == "csr")     { out.values.push_back(read_register(REG_CSR)); return true; }
    if (field == "rud")     { out.values.push_back(m_rud1); out.values.push_back(m_rud2); return true; }
    if (field == "scroll")  { out.values.push_back(m_scroll); return true; }
    if (field == "base")    { out.values.push_back(m_base); return true; }
    if (field == "frames")  { out.values.push_back(m_frames); return true; }
    if (field == "vectors") { out.values.push_back(m_vectors); return true; }
    out.width = 0;
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void E85Video::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("csr", m_csr);
    w.u("rud1", m_rud1);
    w.u("rud2", m_rud2);
    w.array("ruc", m_ruc, 8);
    w.u("scroll", m_scroll);
    w.u("x", m_x);
    w.u("y", m_y);
    w.u("pattern", m_pattern);
    w.u("base", m_base);
    w.n("busy", m_busy);
    w.b("vblank", m_vblank);
    w.b("even", m_even);
    w.n("frames", m_frames);
    w.n("vectors", m_vectors);
    w.hex("plane_b", reinterpret_cast<const uint8_t*>(m_planes[0].data()), WORDS * 2);
    if (m_expansion) {
        w.hex("plane_g", reinterpret_cast<const uint8_t*>(m_planes[1].data()), WORDS * 2);
        w.hex("plane_r", reinterpret_cast<const uint8_t*>(m_planes[2].data()), WORDS * 2);
    }
}

emulator::Result E85Video::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("csr", m_csr);
    r.u("rud1", m_rud1);
    r.u("rud2", m_rud2);
    r.array("ruc", m_ruc, 8);
    r.u("scroll", m_scroll);
    r.u("x", m_x);
    r.u("y", m_y);
    r.u("pattern", m_pattern);
    r.u("base", m_base);
    r.u("busy", m_busy);
    r.b("vblank", m_vblank);
    r.b("even", m_even);
    r.u("frames", m_frames);
    r.u("vectors", m_vectors);
    r.hex("plane_b", reinterpret_cast<uint8_t*>(m_planes[0].data()), WORDS * 2);
    if (m_expansion) {
        r.hex("plane_g", reinterpret_cast<uint8_t*>(m_planes[1].data()), WORDS * 2);
        r.hex("plane_r", reinterpret_cast<uint8_t*>(m_planes[2].data()), WORDS * 2);
    }
    return emulator::Result::ok();
}

ComputerDevice * create_e85_video(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Video(im, cd);
}

//------------------------------ E85Display -----------------------------------

E85Display::E85Display(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
    , m_rgba(4096, 0)
{
    m_clocked = true;
    sx = 1024;
    sy = E85Video::LINES;
    memset(m_dirty, 1, sizeof(m_dirty));
}

emulator::Result E85Display::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;
    const std::string video = cd->get_parameter("video", false).value;
    m_video = dynamic_cast<E85Video*>(im->dm->get_device_by_name(video, false));
    if (m_video == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{E85Display|" + std::string(QT_TRANSLATE_NOOP("E85Display", "The video controller is expected in 'video'")) + "} " + name);
    m_video->attach_display(this);
    update_timing();
    return emulator::Result::ok();
}

void E85Display::reset(bool cold)
{
    GenericDisplay::reset(cold);
    m_pos = 0;
    m_line = 0;
    m_redraw = true;
}

void E85Display::state_restored()
{
    GenericDisplay::state_restored();
    m_redraw = true;
}

// Палитра - по четыре разряда на цвет: в нормальном режиме страницы дают
// яркость до 16 градаций каждая, карта цвета уже
void E85Display::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    std::vector<uint8_t> colors(4096 * 3);
    for (unsigned int i = 0; i < 4096; i++) {
        colors[i * 3 + 0] = (uint8_t)(((i >> 8) & 15) * 17);
        colors[i * 3 + 1] = (uint8_t)(((i >> 4) & 15) * 17);
        colors[i * 3 + 2] = (uint8_t)((i & 15) * 17);
    }
    vr.FillRGB(reinterpret_cast<const uint8_t (*)[3]>(colors.data()), m_rgba.data(), 4096);
}

void E85Display::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

// ТО3, таблица 4: 525 или 625 строк в кадре, в прогрессивной развёртке на одну больше.
// Картину разряд не меняет: оба полукадра идут по тем же строкам памяти
unsigned int E85Display::frame_lines() const
{
    const unsigned int lines = m_video->pal() ? FRAME_LINES_625 : FRAME_LINES_525;
    return m_video->interlaced() ? lines : lines + 1;
}

void E85Display::update_timing()
{
    if (m_video == nullptr) return;
    const unsigned int line = m_system_clock * 2 / (frame_lines() * (m_video->pal() ? 50 : 60));
    m_line_clocks = (line != 0) ? line : 1;
}

// Картина выдаётся за полукадр, а их в кадре два и в прогрессивной развёртке
bool E85Display::native_frame_rate(uint64_t &num, uint64_t &den)
{
    return reduced_rate((uint64_t)m_system_clock * 2, frame_clocks(), num, den);
}

void E85Display::line_done()
{
    if (m_video == nullptr) return;
    const unsigned int visible = m_video->pal() ? VISIBLE_625 : VISIBLE_525;
    // Полукадры чересстрочной развёртки на строку различаются, прогрессивной - равны
    const unsigned int total = (frame_lines() + (m_video->even() ? 1 : 0)) / 2;

    // Запрос посреди кадра исполняет следующий: строки выше луча уже прошли
    if (m_line == 0) {
        m_full_pass = m_redraw;
        m_redraw = false;
    }
    if (m_line < visible) render_line(m_line);
    m_line++;
    if (m_line == visible) {
        // Под растром после перехода с 625 на 525 осталась бы прежняя картина
        if (m_full_pass)
            for (unsigned int line = visible; line < E85Video::LINES; line++) render_line(line);
        if (++m_since_full >= FULL_EVERY) {
            m_since_full = 0;
            m_redraw = true;
        }
        screen_valid = true;
        was_updated = true;
        frame_complete();
        m_video->frame_end();
    }
    if (m_line >= total) {
        m_line = 0;
        m_video->frame_start();
        update_timing();
    }
}

void E85Display::clock(unsigned int counter)
{
    m_pos += counter;
    while (m_pos >= m_line_clocks) {
        m_pos -= m_line_clocks;
        line_done();
    }
}

void E85Display::render_all(MAYBE_UNUSED bool force_render)
{
    if (!screen_valid)
        for (unsigned int line = 0; line < sy; line++)
            render_line_unlocked(line);
    screen_valid = true;
    was_updated = true;
}

void E85Display::render_line(unsigned int line)
{
    const unsigned int physical = (line + m_video->scroll()) & (E85Video::LINES - 1);
    if (!m_full_pass && !m_dirty[physical]) return;
    m_dirty[physical] = false;
    compat_lock_guard guard(m_surface_mutex);
    render_line_unlocked(line);
}

// Уровни страницы вдоль строки: разрешение 1024, 512 или 256 точек даёт 2, 4 или
// 16 градаций. Младший разряд группы левее (Xhomer пишет так же), 0 - тёмная
static void plane_levels(const uint16_t * words, unsigned int resolution, uint8_t * out)
{
    if (resolution == 3) {
        memset(out, 0, 1024);
        return;
    }
    const unsigned int bits = 1u << resolution;
    const unsigned int mask = (1u << bits) - 1;
    const unsigned int scale = 15 / mask;
    for (unsigned int i = 0; i < E85Video::LINE_WORDS; i++) {
        const unsigned int w = words[i];
        for (unsigned int k = 0; k < 16; k += bits) {
            const uint8_t level = (uint8_t)(((w >> k) & mask) * scale);
            for (unsigned int n = 0; n < bits; n++) *out++ = level;
        }
    }
}

void E85Display::render_line_unlocked(unsigned int line)
{
    if (!has_valid_renderer() || render_pixels == nullptr || m_video == nullptr) return;
    if ((int)(sx * 4) > line_bytes) return;

    uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + line * line_bytes);
    if (line >= (m_video->pal() ? (unsigned int)VISIBLE_625 : (unsigned int)VISIBLE_525)) {
        for (unsigned int i = 0; i < sx; i++) *p++ = m_rgba[0];
        return;
    }

    const unsigned int physical = (line + m_video->scroll()) & (E85Video::LINES - 1);
    const uint16_t * blue = m_video->plane(0) + (physical << 6);

    if (!m_video->expansion()) {
        // Монохромный выход НВ1
        uint8_t levels[1024];
        plane_levels(blue, (m_video->rud1() >> 3) & 3, levels);
        for (unsigned int i = 0; i < 1024; i++) *p++ = m_rgba[levels[i] * 0x111];
        return;
    }

    const uint16_t * green = m_video->plane(1) + (physical << 6);
    const uint16_t * red = m_video->plane(2) + (physical << 6);

    if (m_video->color_map()) {
        uint32_t map[8];
        for (unsigned int i = 0; i < 8; i++) {
            const unsigned int v = m_video->ruc(i);
            const unsigned int r = (v >> 5) & 7, g = (v >> 2) & 7, b = v & 3;
            map[i] = m_rgba[(((r << 1) | (r >> 2)) << 8) | (((g << 1) | (g >> 2)) << 4) | ((b << 2) | b)];
        }
        for (unsigned int i = 0; i < E85Video::LINE_WORDS; i++) {
            const unsigned int r = red[i], g = green[i], b = blue[i];
            for (unsigned int k = 0; k < 16; k++)
                *p++ = map[(((r >> k) & 1) << 2) | (((g >> k) & 1) << 1) | ((b >> k) & 1)];
        }
        return;
    }

    uint8_t lr[1024], lg[1024], lb[1024];
    plane_levels(red, (m_video->rud2() >> 11) & 3, lr);
    plane_levels(green, (m_video->rud2() >> 3) & 3, lg);
    plane_levels(blue, (m_video->rud1() >> 3) & 3, lb);
    for (unsigned int i = 0; i < 1024; i++) *p++ = m_rgba[(lr[i] << 8) | (lg[i] << 4) | lb[i]];
}

std::vector<DeviceFieldInfo> E85Display::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"line", "Raster line the beam is on", false});
    r.push_back({"frame_clocks", "Clocks of the display's domain per frame", false});
    return r;
}

bool E85Display::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "line") {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_line);
        return true;
    }
    if (field == "frame_clocks") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back(frame_clocks());
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

void E85Display::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.n("pos", m_pos);
    w.n("line", m_line);
}

emulator::Result E85Display::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.u("pos", m_pos);
    r.u("line", m_line);
    update_timing();
    return emulator::Result::ok();
}

ComputerDevice * create_e85_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Display(im, cd);
}
