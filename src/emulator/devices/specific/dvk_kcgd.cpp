// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: КЦГД (ДВК colour graphics display controller): video memory, registers, video output

#include <cstring>

#include "dvk_kcgd.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3

#define HIRES_BIT       (1u << 16)

#define CSR_PAGE        0000001
#define CSR_INTERLACE   0000002
#define CSR_IEB         0000040
#define CSR_IEA         0000100
#define CSR_REQA        0000200
#define CSR_REQB        0100000
#define CSR_WRITE       (CSR_PAGE | CSR_INTERLACE | CSR_IEB | CSR_IEA)

#define CTRL_INV_REQA   0100
#define CTRL_LORES      0200

#define WINDOW_END      0100000     // окно видеопамяти 000000-077777
#define REG_RA_BASE     0160000
#define REG_RA_END      0164000
#define REG_PIC_BASE    0167770

//=============================== КЦГД ======================================//

KCGD::KCGD(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_evnt(this, im, 1, "evnt", MODE_W)
{
    m_clocked = true;   // развёртка
    can_read = true;
    can_write = true;
    addresable_size = 0200000;
    m_vram.assign(VRAM_WORDS, 0);
}

emulator::Result KCGD::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_vector_a = read_confg_value(cd, "vector_a", false, (unsigned int)0300);
    m_vector_b = read_confg_value(cd, "vector_b", false, (unsigned int)0304);
    const std::string mouse = cd->get_parameter("mouse", false).value;
    m_mouse = nullptr;
    if (!mouse.empty()) {
        m_mouse = dynamic_cast<KCGDMouse*>(im->dm->get_device_by_name(mouse, false));
        if (m_mouse == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{KCGD|" + std::string(QT_TRANSLATE_NOOP("KCGD", "KCGD mouse device is expected")) + "} " + mouse);
    }
    // Развёртка - из образов ПЗУ D61, D62, D63 (по 256 тетрад)
    std::vector<uint8_t> prom[3];
    static const char * const names[3] = {"image_d61", "image_d62", "image_d63"};
    for (unsigned i = 0; i < 3; i++) {
        const std::string name = cd->get_parameter(names[i]).value;
        const std::string file = find_file_location(sd, name);
        if (file.empty())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{KCGD|" + std::string(QT_TRANSLATE_NOOP("KCGD", "File not found")) + "} " + name);
        const std::string data = dsk_tools::utf8_read_file(file);
        if (data.size() != 256)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{KCGD|" + std::string(QT_TRANSLATE_NOOP("KCGD", "A scan PROM image must be 256 bytes")) + "} " + name);
        prom[i].assign(data.begin(), data.end());
    }
    // Какой выход D63 идёт на EVNT: по нетлисту V9 (разряд 1)
    m_evnt_bit = read_confg_value(cd, "evnt_bit", false, (unsigned int)1) & 3;
    res = build_scan(prom[0], prom[1], prom[2]);
    if (!res) return res;
    // Свою видеопамять плата отдаёт процессору в окнах развёртки
    wait_source = this;
    i_virq.change(1);
    i_evnt.change(1);
    return emulator::Result::ok();
}

// Прогон развёртки по полузнакоместам (8 тактов 30,8 МГц = 2 такта
// процессора). Выходы ПЗУ - открытый коллектор с подтяжкой: невыбранная
// микросхема отдаёт единицы. Разряд 0 тетрады образа - вывод 12 микросхемы,
// разряд 3 - вывод 9: только так строб строки D62 (V6, вывод 10) приходится
// на её конец, а не на всю строку. Защёлки берут выходы ПЗУ в конце
// полузнакоместа (D68, D69 по U31n) или знакоместа (D70 по U33)
emulator::Result KCGD::build_scan(const std::vector<uint8_t> &d61, const std::vector<uint8_t> &d62,
                                  const std::vector<uint8_t> &d63)
{
    const auto fail = [this](const char * what) {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{KCGD|" + std::string(what) + "} " + name);
    };

    // Строка: знакоместа до первого V6 (D62, разряд 2). Защёлкнутый в конце
    // знакоместа, он сбрасывает счётчик Q1-Q7 (U41) и считает строку
    unsigned line = 0;
    for (unsigned q = 0; q < 128 && line == 0; q++) {
        if (d62[q * 2] & 4) return fail(QT_TRANSLATE_NOOP("KCGD", "The line ends in the middle of a character cell"));
        if (d62[q * 2 + 1] & 4) line = q + 1;
    }
    if (line == 0) return fail(QT_TRANSLATE_NOOP("KCGD", "The scan PROM never ends a line"));
    m_line_clocks = line * 4;

    // Окно процессора: D70 в конце знакоместа берёт U37 - разряд 3 D61, на
    // открытом коллекторе вместе со стробом данных и выбором видеопамяти.
    // Взятая единица (U1) отдаёт память процессору, а RPLY (D30 по U38)
    // встаёт в конце знакоместа, где взят снова ноль
    std::vector<bool> win(line);
    bool any = false;
    for (unsigned s = 0; s < line; s++) any |= (win[s] = (d61[s * 2 + 1] & 8) != 0);
    if (!any) return fail(QT_TRANSLATE_NOOP("KCGD", "The scan PROM gives the processor no memory window"));
    m_reply.assign(m_line_clocks, 0);
    for (unsigned t = 0; t < m_line_clocks; t++) {
        const unsigned s = t / 4;           // его конец - первый после строба
        unsigned n = 0;
        while (!win[(s + n) % line]) n++;
        while (win[(s + n) % line]) n++;
        m_reply[t] = (uint16_t)(4 * (s + n + 1) - t);
    }

    // Поле. D59/D60 считают строки (по U41, загрузка 061 по U45), D85.1
    // переключается переносом (Q16 - его инверсный выход) и ставится в 1 по
    // U45, D85.2 переключается фронтом D85.1 (Q17 - инверсный выход). D63
    // выбран, пока D85.1 в нуле; адрес - Q7, Q8-Q13, Q17. В конце знакоместа
    // D70 берёт его V10 (U45, загрузка) и выход на EVNT. Прогон - с
    // чересстрочной развёрткой, чтобы получить поля обоих полукадров
    const unsigned max_steps = line * 2 * 2048;
    std::vector<uint8_t> ev(max_steps), rb(max_steps), qq(max_steps);
    std::vector<uint16_t> hp(max_steps);
    std::vector<unsigned> loads;
    unsigned q = 0, half = 0, c = 0, ff1 = 1, q17 = 0, evnt = 1;
    for (unsigned k = 0; k < max_steps && loads.size() < 3; k++) {
        hp[k] = (uint16_t)((q * 2 + half) * 2);
        bool load = false;
        if (half) {
            const unsigned v = ff1 ? 0xF : (d63[((q >> 6) & 1) | ((c & 63) << 1) | (q17 << 7)] & 0xF);
            evnt = (v >> m_evnt_bit) & 1;
            const bool line_end = (q + 1 == line);
            q = line_end ? 0 : q + 1;
            if (line_end) {
                if (c == 255) {
                    c = 0;
                    ff1 ^= 1;
                    if (ff1) q17 ^= 1;
                } else c++;
            }
            if ((v & 1) == 0) {
                c = 061;
                if (!ff1) { ff1 = 1; q17 ^= 1; }
                load = true;
            }
        }
        ev[k] = (uint8_t)evnt;
        rb[k] = (uint8_t)((c >> 4) & 1);
        qq[k] = (uint8_t)q17;
        if (load) loads.push_back(k);
        half ^= 1;
    }
    if (loads.size() < 3) return fail(QT_TRANSLATE_NOOP("KCGD", "The scan PROM never ends a field"));

    // Поле - от загрузки до загрузки; смена в конце шага k действует с
    // момента 2 * (k - начало поля)
    for (unsigned f = 0; f < 2; f++) {
        const unsigned a = loads[f], b = loads[f + 1];
        std::vector<ScanStep> &steps = m_field[qq[a]];
        steps.clear();
        steps.push_back({0, ev[a], rb[a]});
        for (unsigned k = a + 1; k < b; k++)
            if (ev[k] != ev[k - 1] || rb[k] != rb[k - 1])
                steps.push_back({2 * (k - a), ev[k], rb[k]});
        m_field_clocks[qq[a]] = 2 * (b - a);
        if (qq[a] == 0) m_field_hpos = hp[a + 1];
    }
    if (m_field[0].empty() || m_field[1].empty())
        return fail(QT_TRANSLATE_NOOP("KCGD", "The scan PROM gives no interlaced fields"));
    return emulator::Result::ok();
}

void KCGD::scan_apply(unsigned evnt, unsigned reqb)
{
    if (i_evnt.value != evnt) i_evnt.change(evnt);
    if ((reqb != 0) != m_reqb) {
        m_reqb = reqb != 0;
        update_requests();
    }
}

// Найти место в таблице поля по m_fpos; apply - выставить линии, как там
void KCGD::scan_seek(bool apply)
{
    const std::vector<ScanStep> &f = m_field[m_q17];
    unsigned i = 0;
    while (i + 1 < f.size() && f[i + 1].t <= m_fpos) i++;
    m_step = i + 1;
    if (apply) scan_apply(f[i].evnt, f[i].reqb);
}

// Обращение процессора КЦГД: offset - его строб данных от точки, до
// которой развёртка досчитана. Видеопамять отвечает в окне, остальное -
// как обычно (0)
unsigned int KCGD::wait_states(unsigned int address, MAYBE_UNUSED bool write, unsigned int offset)
{
    const unsigned a = address & 0177776;
    if (a >= WINDOW_END && !(a >= REG_RA_BASE && a < REG_RA_END && (a & 2))) return 0;
    return m_reply[(m_hpos + offset) % m_line_clocks];
}

void KCGD::reset_registers()
{
    m_csr &= ~(CSR_PAGE | CSR_INTERLACE | CSR_IEB | CSR_IEA);
    m_pending_a = m_pending_b = false;
    m_line_a = m_line_b = false;
    update_irq();
}

void KCGD::reset(bool cold)
{
    AddressableDevice::reset(cold);
    if (cold) {
        std::fill(m_vram.begin(), m_vram.end(), 0);
        for (unsigned i = 0; i < 16; i++) m_palette[i] = 0;
        m_ra = 0;
        m_control = 0;
        m_out = 0;
        m_reqa = false;
        m_csr = 0;
        // Развёртку INIT не трогает; при включении она - в начале поля
        m_q17 = 0;
        m_fpos = 0;
        m_hpos = m_field_hpos;
        m_fields = 0;
    }
    m_irq.clear();
    i_virq.change(1);
    reset_registers();
    if (cold) scan_seek(true);
}

void KCGD::clock(unsigned int counter)
{
    m_hpos = (m_hpos + counter) % m_line_clocks;
    m_fpos += counter;
    for (;;) {
        const std::vector<ScanStep> &f = m_field[m_q17];
        while (m_step < f.size() && f[m_step].t <= m_fpos) {
            scan_apply(f[m_step].evnt, f[m_step].reqb);
            m_step++;
        }
        if (m_fpos < m_field_clocks[m_q17]) break;
        m_fpos -= m_field_clocks[m_q17];
        m_fields++;
        // Без чересстрочной развёртки D85.2 держится установленным (Q17 = 0)
        m_q17 = interlace() ? (m_q17 ^ 1) : 0;
        m_step = 0;
    }
}

void KCGD::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_INIT) {
        if (new_value & 1) reset_registers();
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        const unsigned vector = new_value & 0xFFFF;
        if (vector == 0) return;
        if (vector == m_vector_a) m_pending_a = false;
        if (vector == m_vector_b) m_pending_b = false;
    }
    update_irq();
}

void KCGD::update_irq()
{
    const unsigned own = m_pending_a ? m_vector_a : (m_pending_b ? m_vector_b : 0);
    m_irq.offer(VirqLine::chain(own, i_virq_in, i_vector_in));
}

// REQA - 17-й разряд последнего слова, прочитанного через 160002, исключающее
// ИЛИ с разрядом 6 управления в тот момент, когда его смотрят: заводской тест
// KC.SAV (ошибка 130) переключает разряд 6 и сразу ждёт другое REQA
bool KCGD::reqa() const
{
    return m_reqa != ((m_control & CTRL_INV_REQA) != 0);
}

// Запрос - по появлению условия «разрешено и поднято», в том числе когда
// разрешают при уже поднятом REQA (ошибка 132 того же теста); запрет снимает
void KCGD::update_requests()
{
    const bool a = (m_csr & CSR_IEA) && reqa();
    const bool b = (m_csr & CSR_IEB) && m_reqb;
    if (a && !m_line_a) m_pending_a = true;
    if (b && !m_line_b) m_pending_b = true;
    if (!(m_csr & CSR_IEA)) m_pending_a = false;
    if (!(m_csr & CSR_IEB)) m_pending_b = false;
    m_line_a = a;
    m_line_b = b;
    update_irq();
}

void KCGD::write_vram(unsigned index, unsigned value, unsigned mask)
{
    uint32_t &w = m_vram[index & (VRAM_WORDS - 1)];
    w = (w & ~mask & 0xFFFF) | (value & mask);
    if (m_control & CTRL_LORES) w |= HIRES_BIT;
}

unsigned int KCGD::get_value_word(unsigned int address)
{
    address &= 0177776;
    if (address < WINDOW_END) return m_vram[address >> 1] & 0xFFFF;
    if (address >= REG_RA_BASE && address < REG_RA_END) {
        if ((address & 2) == 0) return m_ra;
        const uint32_t w = m_vram[m_ra];
        m_reqa = ((w >> 16) & 1) != 0;
        update_requests();
        return w & 0xFFFF;
    }
    switch (address & 7) {
    case 0: return (m_csr & CSR_WRITE) | (reqa() ? CSR_REQA : 0) | (m_reqb ? CSR_REQB : 0);
    // Младший байт - управление, старший - регистр палитры, номер которого
    // в управлении: регистры читаются (заводской тест KC.SAV, ошибки 40-43)
    case 2: return (m_control & 0xFF) | (m_palette[(m_control >> 2) & 15] << 8);
    case 4:
        // Мышь - на AD08-AD11, ось выбирает разряд 2 управления
        return (m_mouse != nullptr) ? (m_mouse->value((m_control >> 2) & 1) << 8) : 0;
    default: return 0;
    }
}

// Отладчик и LOG: регистр данных - без REQA
unsigned KCGD::get_direct(unsigned address)
{
    unsigned w;
    const unsigned a = address & 0177776;
    if (a >= REG_RA_BASE && a < REG_RA_END && (a & 2)) w = m_vram[m_ra] & 0xFFFF;
    else w = get_value_word(a);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int KCGD::get_value(unsigned int address)
{
    const unsigned w = get_value_word(address);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void KCGD::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    address &= 0177776;
    value &= 0xFFFF;
    if (address < WINDOW_END) { write_vram(address >> 1, value, 0xFFFF); return; }
    if (address >= REG_RA_BASE && address < REG_RA_END) {
        if ((address & 2) == 0) m_ra = value;
        else write_vram(m_ra, value, 0xFFFF);
        return;
    }
    switch (address & 7) {
    case 0:
        m_csr = (m_csr & ~CSR_WRITE) | (value & CSR_WRITE);
        update_requests();
        if (!interlace() && m_q17 != 0) {
            m_q17 = 0;
            scan_seek(true);
        }
        break;
    case 2:
        m_out = value;
        m_control = value & 0xFF;
        m_palette[(m_control >> 2) & 15] = (value >> 8) & 0xFF;
        update_requests();
        break;
    default:
        break;
    }
}

// Байтовая запись: в видеопамять - половина слова; в 167772 - только своя
// половина (младший байт - управление, старший - регистр палитры по номеру
// из управления), так её и пишет прошивка
void KCGD::set_value(unsigned int address, unsigned int value, bool force)
{
    const unsigned a = address & 0177776;
    const bool high = (address & 1) != 0;
    value &= 0xFF;
    if (a < WINDOW_END) { write_vram(a >> 1, high ? (value << 8) : value, high ? 0xFF00 : 0x00FF); return; }
    if (a >= REG_RA_BASE && a < REG_RA_END) {
        if ((a & 2) == 0) m_ra = high ? ((m_ra & 0x00FF) | (value << 8)) : ((m_ra & 0xFF00) | value);
        else write_vram(m_ra, high ? (value << 8) : value, high ? 0xFF00 : 0x00FF);
        return;
    }
    if ((a & 7) == 2) {
        if (high) {
            m_out = (m_out & 0x00FF) | (value << 8);
            m_palette[(m_control >> 2) & 15] = value & 0xFF;
        } else {
            m_out = (m_out & 0xFF00) | value;
            m_control = value;
            update_requests();
        }
        return;
    }
    if ((a & 7) == 0 && !high) {
        set_value_word(a, (m_csr & 0xFF00) | value, force);
    }
}

std::vector<DeviceFieldInfo> KCGD::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"ra",      "Регистр адреса видеопамяти (160000)", false});
    r.push_back({"csr",     "Регистр 167770", false});
    r.push_back({"control", "Регистр управления (младший байт 167772)", false});
    r.push_back({"palette", "Регистры палитры 0-15", false});
    r.push_back({"vram",    "Слова видеопамяти, vram(с,по), с 17-м разрядом", false});
    r.push_back({"scan",    "Развёртка: строка поля, такт строки, полукадр Q17, полей с пуска", false});
    return r;
}

bool KCGD::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "ra")      { out.values.push_back(m_ra); return true; }
    if (field == "csr")     { out.values.push_back(get_value_word(REG_PIC_BASE)); return true; }
    if (field == "control") { out.values.push_back(m_control); return true; }
    if (field == "palette") { for (unsigned i = 0; i < 16; i++) out.values.push_back(m_palette[i]); return true; }
    if (field == "scan") {
        out.values.push_back(m_fpos / m_line_clocks);
        out.values.push_back(m_hpos);
        out.values.push_back(m_q17);
        out.values.push_back(m_fields);
        return true;
    }
    if (field == "vram") {
        out.width = 32;
        if (to < from) to = from;
        for (unsigned i = from; i <= to && i < VRAM_WORDS; i++) out.values.push_back(m_vram[i]);
        return true;
    }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

void KCGD::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    std::vector<uint8_t> lo(VRAM_WORDS * 2), hi(VRAM_WORDS / 8, 0);
    for (unsigned i = 0; i < VRAM_WORDS; i++) {
        lo[i * 2] = m_vram[i] & 0xFF;
        lo[i * 2 + 1] = (m_vram[i] >> 8) & 0xFF;
        if (m_vram[i] & HIRES_BIT) hi[i >> 3] |= (uint8_t)(1 << (i & 7));
    }
    w.hex("vram", lo.data(), lo.size(), 16);
    w.hex("vram17", hi.data(), hi.size());
    w.u("ra", m_ra);
    w.u("out", m_out);
    w.array("palette", m_palette, 16);
    w.u("csr", m_csr);
    w.b("reqa", m_reqa);
    w.b("reqb", m_reqb);
    w.b("pending_a", m_pending_a);
    w.b("pending_b", m_pending_b);
    w.b("line_a", m_line_a);
    w.b("line_b", m_line_b);
    w.n("hpos", m_hpos);
    w.n("fpos", m_fpos);
    w.n("q17", m_q17);
    w.n("fields", m_fields);
    w.u("offered", m_irq.offered());
}

emulator::Result KCGD::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    std::vector<uint8_t> lo(VRAM_WORDS * 2, 0), hi(VRAM_WORDS / 8, 0);
    if (r.hex("vram", lo.data(), lo.size(), 16)) {
        r.hex("vram17", hi.data(), hi.size());
        for (unsigned i = 0; i < VRAM_WORDS; i++)
            m_vram[i] = lo[i * 2] | (lo[i * 2 + 1] << 8) | (((hi[i >> 3] >> (i & 7)) & 1) ? HIRES_BIT : 0);
    }
    r.u("ra", m_ra);
    r.u("out", m_out);
    m_control = m_out & 0xFF;
    r.array("palette", m_palette, 16);
    r.u("csr", m_csr);
    r.b("reqa", m_reqa);
    r.b("reqb", m_reqb);
    r.b("pending_a", m_pending_a);
    r.b("pending_b", m_pending_b);
    r.b("line_a", m_line_a);
    r.b("line_b", m_line_b);
    r.u("hpos", m_hpos);
    r.u("fpos", m_fpos);
    r.u("q17", m_q17);
    r.u("fields", m_fields);
    m_hpos %= m_line_clocks;
    m_q17 &= 1;
    scan_seek(false);
    unsigned offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    return emulator::Result::ok();
}

ComputerDevice * create_kcgd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new KCGD(im, cd);
}

//================================ Мышь ======================================//

// Больше стольких шагов в запасе не копится: рывок хоста не должен потом
// долго дотягиваться
#define KCGD_MOUSE_MAX_PENDING  64

KCGDMouse::KCGDMouse(InterfaceManager *im, EmulatorConfigDevice *cd):
      ComputerDevice(im, cd)
    , m_dx(0)
    , m_dy(0)
    , m_buttons(0)
{
    m_clocked = true;
    device_class = "mouse";
}

emulator::Result KCGDMouse::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    // Шаг - время, за которое шар даёт один импульс; столько же программа
    // должна успеть опросить счётчик хотя бы раз за 4 шага
    const unsigned us = read_confg_value(cd, "step_us", false, (unsigned int)4000);
    m_period = (unsigned)((uint64_t)m_system_clock * us / 1000000);
    if (m_period == 0) m_period = 1;
    return emulator::Result::ok();
}

void KCGDMouse::reset(bool cold)
{
    ComputerDevice::reset(cold);
    if (cold) { m_x = m_y = 0; m_ticks = 0; }
}

void KCGDMouse::add_pending(std::atomic<int> &axis, int delta)
{
    int current = axis.load();
    int value;
    do {
        value = current + delta;
        if (value > KCGD_MOUSE_MAX_PENDING) value = KCGD_MOUSE_MAX_PENDING;
        if (value < -KCGD_MOUSE_MAX_PENDING) value = -KCGD_MOUSE_MAX_PENDING;
    } while (!axis.compare_exchange_weak(current, value));
}

void KCGDMouse::move(int dx, int dy, int buttons)
{
    if (buttons >= 0) m_buttons = static_cast<unsigned>(buttons) & 3;
    if (dx != 0) add_pending(m_dx, dx);
    if (dy != 0) add_pending(m_dy, dy);
}

// Поток эмуляции: шаг по каждой оси, не чаще одного за период
void KCGDMouse::clock(unsigned int counter)
{
    m_ticks += counter;
    if (m_ticks < m_period) return;
    m_ticks -= m_period;
    if (m_ticks >= m_period) m_ticks = 0;   // простой не копит шаги
    const int dx = m_dx.load(), dy = m_dy.load();
    if (dx > 0) { m_x = (m_x + 1) & 15; add_pending(m_dx, -1); }
    if (dx < 0) { m_x = (m_x - 1) & 15; add_pending(m_dx, 1); }
    if (dy > 0) { m_y = (m_y + 1) & 15; add_pending(m_dy, -1); }
    if (dy < 0) { m_y = (m_y - 1) & 15; add_pending(m_dy, 1); }
}

unsigned KCGDMouse::value(unsigned axis) const
{
    const unsigned buttons = m_buttons.load();
    if (axis == 0) return (m_x & 7) | ((buttons & 1) ? 010 : 0);
    return (m_y & 7) | ((buttons & 2) ? 010 : 0);
}

std::vector<DeviceFieldInfo> KCGDMouse::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"x",       "Счётчик X (4 разряда, видны 3)", false});
    r.push_back({"y",       "Счётчик Y (4 разряда, видны 3)", false});
    r.push_back({"buttons", "Кнопки: разряд 0 - левая, 1 - правая", false});
    r.push_back({"pending", "Шаги, ещё не отданные счётчикам, по обеим осям", false});
    return r;
}

bool KCGDMouse::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "x")       { out.values.push_back(m_x); return true; }
    if (field == "y")       { out.values.push_back(m_y); return true; }
    if (field == "buttons") { out.values.push_back(m_buttons.load()); return true; }
    if (field == "pending") {
        const int x = m_dx.load(), y = m_dy.load();
        out.values.push_back(static_cast<unsigned>((x < 0 ? -x : x) + (y < 0 ? -y : y)));
        return true;
    }
    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

// Шаги в запасе и кнопки - ввод хоста, в снимок не идут
void KCGDMouse::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.u("x", m_x);
    w.u("y", m_y);
    w.n("ticks", m_ticks);
}

emulator::Result KCGDMouse::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.u("x", m_x);
    r.u("y", m_y);
    r.u("ticks", m_ticks);
    m_dx = 0;
    m_dy = 0;
    m_buttons = 0;
    return emulator::Result::ok();
}

ComputerDevice * create_kcgd_mouse(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new KCGDMouse(im, cd);
}

//=========================== Изображение ===================================//

#define KCGD_WIDTH      800
#define KCGD_HEIGHT     480
#define KCGD_LINE_WORDS 100
#define KCGD_TABLE_0    (015574 >> 1)
#define KCGD_TABLE_1    (005574 >> 1)
#define KCGD_FONT_SIZE  (256 * 10)

KCGDDisplay::KCGDDisplay(InterfaceManager *im, EmulatorConfigDevice *cd):
      GenericDisplay(im, cd)
{
    m_clocked = true;
    sx = KCGD_WIDTH;
    sy = KCGD_HEIGHT;
}

emulator::Result KCGDDisplay::load_config(SystemData *sd)
{
    emulator::Result res = GenericDisplay::load_config(sd);
    if (!res) return res;

    m_kcgd = dynamic_cast<KCGD*>(im->dm->get_device_by_name(cd->get_parameter("kcgd").value, false));
    if (m_kcgd == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{KCGDDisplay|" + std::string(QT_TRANSLATE_NOOP("KCGDDisplay", "KCGD device is expected")) + "} " + name);

    // Шрифт прошивки - для поля text: 256 знаков 8 x 10 с этого места ПЗУ
    const std::string font = cd->get_parameter("font", false).value;
    if (!font.empty()) {
        m_font_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(font, false));
        if (m_font_rom == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{KCGDDisplay|" + std::string(QT_TRANSLATE_NOOP("KCGDDisplay", "Character generator device is expected")) + "} " + font);
        m_font_offset = read_confg_value(cd, "font_offset", false, (unsigned int)012236);
        m_font_column = read_confg_value(cd, "font_column", false, (unsigned int)2);
        // Таблица КОИ-7 Н0 прошивки: код 040-0177 -> номер знака шрифта
        m_font_map_offset = read_confg_value(cd, "font_map", false, (unsigned int)017236);
        // Таблица КОИ-7 Н1 (кириллица): код 0100-0177 -> номер знака
        m_font_map_h1_offset = read_confg_value(cd, "font_map_h1", false, (unsigned int)017576);
    }

    // Выходы U55, U56, U57 - какие это цвета: строка из букв R, G, B
    const std::string rgb = str_tolower(read_confg_value(cd, "rgb", false, std::string("rgb")));
    if (rgb.size() != 3)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{KCGDDisplay|" + std::string(QT_TRANSLATE_NOOP("KCGDDisplay", "Incorrect parameters for")) + "} rgb");
    for (unsigned i = 0; i < 3; i++) {
        const char c = rgb[i];
        m_channel[i] = (c == 'r') ? 0 : (c == 'g') ? 1 : 2;
    }

    return emulator::Result::ok();
}

void KCGDDisplay::reset(bool cold)
{
    GenericDisplay::reset(cold);
}

// Шрифт копируется при чтении текста, а не в load_config: ПЗУ, объявленное
// после дисплея, к load_config ещё не прочло образ, а переключатель машины
// меняет прошивку уже после сброса
void KCGDDisplay::load_font() const
{
    m_font.assign(KCGD_FONT_SIZE, 0);
    for (unsigned c = 0; c < 96; c++) m_font_map[c] = (uint8_t)(c + 040);
    if (m_font_rom != nullptr) {
        for (unsigned i = 0; i < KCGD_FONT_SIZE; i++)
            m_font[i] = m_font_rom->get_direct(m_font_offset + i) & 0xFF;
        for (unsigned c = 0; c < 96; c++)
            m_font_map[c] = m_font_rom->get_direct(m_font_map_offset + c) & 0xFF;
        for (unsigned c = 0; c < 64; c++)
            m_font_map_h1[c] = m_font_rom->get_direct(m_font_map_h1_offset + c) & 0xFF;
    }
}

// Двухразрядный ЦАП канала: 0, 85, 170, 255
void KCGDDisplay::set_renderer(VideoRenderer &vr)
{
    GenericDisplay::set_renderer(vr);
    uint8_t table[64][3];
    for (unsigned v = 0; v < 64; v++)
        for (unsigned ch = 0; ch < 3; ch++)
            table[v][m_channel[ch]] = (uint8_t)(85 * ((v >> (ch * 2)) & 3));
    vr.FillRGB(table, m_colors, 64);
}

void KCGDDisplay::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    *sx = this->sx;
    *sy = this->sy;
}

void KCGDDisplay::clock(MAYBE_UNUSED unsigned int counter)
{
    if (m_kcgd->fields() == m_field) return;
    m_field = m_kcgd->fields();
    screen_valid = false;
}

// Слово видеопамяти, с которого идёт строка растра y
unsigned KCGDDisplay::line_address(unsigned y) const
{
    const unsigned table = m_kcgd->page() ? KCGD_TABLE_1 : KCGD_TABLE_0;
    const unsigned row = m_kcgd->interlace() ? y : (y & ~1u);
    return m_kcgd->vram(table + (KCGD_HEIGHT - 1) - row) & 0xFFFF;
}

void KCGDDisplay::render_all(MAYBE_UNUSED bool force_render)
{
    if (m_kcgd != nullptr && render_pixels != nullptr) {
        uint32_t pal[16];
        for (unsigned i = 0; i < 16; i++) pal[i] = m_colors[m_kcgd->palette(i)];
        for (unsigned y = 0; y < KCGD_HEIGHT; y++) {
            uint32_t * p = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render_pixels) + y * line_bytes);
            unsigned a = line_address(y);
            for (unsigned i = 0; i < KCGD_LINE_WORDS; i++) {
                const uint32_t w = m_kcgd->vram(a++);
                if (w & HIRES_BIT) {
                    for (int s = 12; s >= 0; s -= 4) {
                        const uint32_t c = pal[(w >> s) & 15];
                        *p++ = c;
                        *p++ = c;
                    }
                } else {
                    for (int s = 14; s >= 0; s -= 2) *p++ = pal[5 * ((w >> s) & 3)];
                }
            }
        }
    }
    screen_valid = true;
    was_updated = true;
}

// Точка x строки растра, начинающейся со слова a: номер регистра палитры
unsigned KCGDDisplay::pixel(unsigned a, unsigned x) const
{
    const uint32_t w = m_kcgd->vram(a + x / 8);
    if (w & HIRES_BIT) return (w >> (12 - 4 * ((x % 8) / 2))) & 15;
    return 5 * ((w >> (14 - 2 * (x % 8))) & 3);
}

// Экран текстом - то, что сценарий сравнивает вместо снимка. Знакоместо
// 10 x 10 точек (80 знаков в строке, 4 знака на 5 слов), знак 8 x 10 с
// font_column точки; узор сличается со шрифтом прошивки. Строка знаков - 10
// соседних строк таблицы (каждая вторая строка растра), так что прокрутка
// таблицей не мешает. Кириллица показывается латиницей того же кода КОИ-7.
// Не найденное в шрифте - точка, пустое место - пробел
std::string KCGDDisplay::screen_text() const
{
    load_font();
    std::string text;
    for (unsigned row = 0; row < 24; row++) {
        unsigned lines[10];
        for (unsigned k = 0; k < 10; k++) lines[k] = line_address((row * 10 + k) * 2);
        std::string s;
        for (unsigned col = 0; col < 80; col++) {
            uint8_t glyph[10];
            bool empty = true;
            for (unsigned k = 0; k < 10; k++) {
                uint8_t b = 0;
                for (unsigned i = 0; i < 8; i++)
                    if (pixel(lines[k], col * 10 + m_font_column + i) != 0) b |= (uint8_t)(0x80 >> i);
                glyph[k] = b;
                if (b) empty = false;
            }
            char c = ' ';
            if (!empty) {
                c = '.';
                // Сначала латиница (Н0); кириллица (Н1) - латиницей того же
                // кода, как поле text у КСМ
                for (unsigned ch = 041; ch < 0177 && c == '.'; ch++)
                    if (memcmp(&m_font[m_font_map[ch - 040] * 10], glyph, 10) == 0) c = (char)ch;
                for (unsigned ch = 0100; ch < 0177 && c == '.'; ch++)
                    if (memcmp(&m_font[m_font_map_h1[ch - 0100] * 10], glyph, 10) == 0) c = (char)ch;
            }
            s += c;
        }
        while (!s.empty() && s.back() == ' ') s.pop_back();
        text += "\n" + s;
    }
    return text;
}

std::vector<DeviceFieldInfo> KCGDDisplay::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = GenericDisplay::get_device_fields();
    r.push_back({"frame", "Полей развёртки с пуска", false});
    r.push_back({"text",  "Текст экрана, 24 строки по 80 знаков (по шрифту прошивки)", false});
    return r;
}

bool KCGDDisplay::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "frame") {
        out.numeric = true;
        out.values.push_back(m_kcgd->fields());
        return true;
    }
    if (field == "text") {
        out.numeric = false;
        out.text = screen_text();
        return true;
    }
    return GenericDisplay::get_field(field, from, to, out);
}

void KCGDDisplay::save_state(StateWriter &w)
{
    GenericDisplay::save_state(w);
    w.n("field", m_field);
}

emulator::Result KCGDDisplay::load_state(const StateReader &r)
{
    emulator::Result res = GenericDisplay::load_state(r);
    if (!res) return res;
    r.u("field", m_field);
    return emulator::Result::ok();
}

ComputerDevice * create_kcgd_display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new KCGDDisplay(im, cd);
}
