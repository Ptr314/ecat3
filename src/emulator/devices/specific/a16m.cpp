// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: А16М floppy controller memory of the БК (AltPro)

#include "a16m.h"
#include "emulator/utils.h"

// Смещения внутри окна 100000-177777
static const unsigned int OFF_REG     = 077130;     // 177130, регистр режима и дисковода
static const unsigned int OFF_FDC_END = 077133;
static const unsigned int OFF_HIGH    = 077000;     // 177000: страница ввода-вывода

// Номер режима - разряды 4-6 кода, то есть код режима / 020
enum {
    M_HLT11 = 0,    // 000 (в описании - 20000; разряд 13 платой не читается)
    M_BASIC = 1,    // 020
    M_RAM11 = 2,    // 040
    M_STD10 = 3,    // 060
    M_RAMZZ = 4,    // 100, ОЗУзз
    M_RAM10 = 5,    // 120
    M_STD11 = 6,    // 140
    M_START = 7     // 160
};

static const int N = -1;    // сегмент платой не занят
static const int R = 8;     // ПЗУ платы

// Что стоит в сегментах 100000..170000 в каждом режиме: сегмент ОЗУ платы,
// ее ПЗУ или ничего. Таблица режимов из описания А16М (A16TECH.TXT Новака),
// сверенная с таблицами COAD16 и COAD16_11 теста контроллеров gid v4.77F.
// Сегмент 0 в режимах 020 и 100 - только на чтение, сегмент 3 в Hlt11 -
// только на запись (write_source())
static const int SEGMENTS[8][8] = {
    //  100000 110000 120000 130000 140000 150000 160000 170000
    {   N,     N,     N,     N,     0,     1,     2,     3 },    // 000 Hlt11, 170000 - только запись
    {   0,     1,     N,     N,     N,     N,     N,     N },    // 020 Basic, 100000 - только чтение
    {   N,     N,     N,     N,     0,     1,     2,     N },    // 040 ОЗУ11
    {   N,     N,     2,     3,     0,     1,     R,     N },    // 060 Std10
    {   0,     1,     2,     3,     N,     N,     R,     N },    // 100 ОЗУзз, 100000 - только чтение
    {   0,     1,     2,     3,     N,     N,     R,     N },    // 120 ОЗУ10
    {   N,     N,     N,     N,     N,     N,     R,     N },    // 140 Std11
    {   N,     N,     2,     3,     0,     1,     R,     R }     // 160 Start, ПЗУ дважды
};

// Сигналы, которыми плата отключает память самой машины (разъем МПИ):
// монитор БК0010 (MON10, 100000-117777), ПЗУ БОС БК0011М (MON11, 140000-157777;
// нужна доработка по контакту B6) и верхнее ОЗУ БК0011М (RAM11, 100000-137777)
static const bool MON10_OFF[8] = { true,  true,  false, false, true,  true,  false, false };
static const bool MON11_OFF[8] = { true,  true,  true,  true,  false, false, false, false };
static const bool UP11_OFF[8]  = { false, true,  false, true,  false, true,  false, true  };

A16M::A16M(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_basic(this, im, 1, "basic", MODE_W)
{
    addresable_size = 040000 * 2;
    can_read = true;
    can_write = true;
}

emulator::Result A16M::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    m_ram = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("rom").value));
    if (m_ram == nullptr || m_rom == nullptr || m_ram->get_size() < 16*1024 || m_rom->get_size() < 4096)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);

    const std::string machine = cd->get_parameter("machine", false).value;
    m_bk11 = (machine == "bk11");

    m_ram_buf = m_ram->is_plain() ? m_ram->get_buffer() : nullptr;
    m_rom_buf = m_rom->get_buffer();
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name("mapper", false));

    return emulator::Result::ok();
}

void A16M::reset(bool cold)
{
    AddressableDevice::reset(cold);
    // Включение питания и долгий сброс ставят режим Start: с ПЗУ платы БК и
    // стартует
    set_register(0160);
    m_strobe = false;
    m_rd_off = false;
    m_basic = false;
    i_basic.change(0);
}

// Новый режим. Ответы route() зависят от режима и от разряда БЕЙСИКа, поэтому
// разобранные диспетчером страницы сбрасываются при смене любого из них
void A16M::set_register(unsigned int value)
{
    const bool mode_changed = ((value ^ m_value) & 0160) != 0;
    m_value = value & 0177777;
    if (mode_changed && m_mapper != nullptr) m_mapper->routing_changed();
}

// Источник байта по смещению в окне для чтения: сегмент ОЗУ, R или N. В режиме
// Start ПЗУ занимает и страницу ввода-вывода: процессор берет адрес пуска из
// 177716, а там читается слово ПЗУ 167716
int A16M::read_source(unsigned int offset) const
{
    const unsigned int m = mode_index();
    if (offset >= OFF_HIGH) return (m == M_START) ? R : N;
    if (m == M_HLT11 && offset >= 070000) return N;
    return SEGMENTS[m][offset >> 12];
}

// Куда ложится запись: сегмент ОЗУ или N. В Hlt11 сегмент 3 принимает запись
// во всем 170000-177777, страница ввода-вывода тоже - так пультовый режим
// процессора сохраняет PC и PSW по 177674 и 177676
int A16M::write_source(unsigned int offset) const
{
    const unsigned int m = mode_index();
    if (offset >= OFF_HIGH) return (m == M_HLT11) ? 3 : N;
    const int src = SEGMENTS[m][offset >> 12];
    if (src == R) return N;
    if ((m == M_BASIC || m == M_RAMZZ) && offset < 010000) return N;
    return src;
}

// Отключена ли сигналом платы собственная память машины в этом сегменте.
// ПЗУ БЕЙСИКа БК0010-01 сюда не входит: его подключает диспетчер по выходу
// basic
bool A16M::base_disabled(unsigned int segment) const
{
    const unsigned int m = mode_index();
    if (m_bk11) {
        if (segment < 4) return UP11_OFF[m];
        if (segment < 6) return MON11_OFF[m];
        return false;
    }
    if (segment < 2) return MON10_OFF[m];
    return false;
}

unsigned int A16M::route(unsigned int address, unsigned int mode)
{
    const unsigned int m = mode_index();

    // Регистры дисковода отвечают сами, а запись в 177130 плата еще и
    // защелкивает. Разряд 3 на БК0010-01 отдает адреса ПЗУ БЕЙСИКа, разряд 2
    // запрещает чтение регистров: отвечает то, что плата ставит на эти
    // адреса, а если ничего - никто
    if (address >= OFF_REG && address <= OFF_FDC_END) {
        if (mode == MODE_W) return ROUTE_THROUGH;
        if (!m_rd_off || (m_basic && !m_bk11)) return ROUTE_PASS;
        return (read_source(address) != N) ? ROUTE_ANSWER : ROUTE_TIMEOUT;
    }

    // Страница ввода-вывода: в Hlt11 плата пишет ее в сегмент 3 вместе с
    // регистрами, в Start ее ПЗУ отвечает на чтение вместе с ними (ИЛИ).
    // 177000-177377 при разряде 3 занимает ПЗУ БЕЙСИКа БК0010-01
    if (address >= OFF_HIGH) {
        if (mode == MODE_W) return (m == M_HLT11) ? ROUTE_THROUGH : ROUTE_PASS;
        if (m_basic && !m_bk11 && address < 077400) return ROUTE_PASS;
        return (m == M_START) ? ROUTE_OR : ROUTE_PASS;
    }

    const unsigned int segment = address >> 12;
    // ПЗУ БЕЙСИКа БК0010-01 занимает 120000-177377 поверх всего, что плата
    // там ставит
    const bool basic = m_basic && !m_bk11 && segment >= 2;

    if (mode != MODE_W) {
        if (basic) return ROUTE_PASS;
        if (read_source(address) == N)
            return base_disabled(segment) ? ROUTE_TIMEOUT : ROUTE_PASS;
        // ПЗУ БОС БК0011М, которое плата не отключила, выставляет данные
        // вместе с ее ОЗУ
        if (m_bk11 && (segment == 4 || segment == 5) && !base_disabled(segment)) return ROUTE_OR;
        return ROUTE_ANSWER;
    }

    if (write_source(address) == N)
        return base_disabled(segment) ? ROUTE_TIMEOUT : ROUTE_PASS;
    // Запись видят все: ОЗУ БК0011М под платой тоже ее получает
    return (m_bk11 && segment < 4 && !base_disabled(segment)) ? ROUTE_THROUGH : ROUTE_ANSWER;
}

unsigned int A16M::read_byte(unsigned int offset, bool direct)
{
    const int src = read_source(offset);
    if (src == N) return _FFFF;
    const unsigned int in_segment = offset & 07777;
    if (src == R) return m_rom_buf[in_segment];
    const unsigned int a = (src << 12) + in_segment;
    if (m_ram_buf != nullptr) return m_ram_buf[a];
    return direct ? m_ram->get_direct(a) : m_ram->get_value(a);
}

void A16M::write_byte(unsigned int offset, unsigned int value)
{
    const int src = write_source(offset);
    if (src == N) return;
    const unsigned int a = (src << 12) + (offset & 07777);
    if (m_ram_buf != nullptr) m_ram_buf[a] = (uint8_t)value;
    else m_ram->set_value(a, value);
}

// Запись в 177130. Значение защелкивается записью, которая идет сразу за
// стробом - записью с единицами в разрядах 1 и 2. Разряды 2 и 3 каждой
// записи запрещают чтение регистров и подключают БЕЙСИК
void A16M::write_177130(unsigned int value)
{
    const bool strobe = (value & 06) == 06;
    if (m_strobe && !strobe) set_register(value);
    m_strobe = strobe;
    m_rd_off = (value & 4) != 0;
    const bool basic = (value & 010) != 0;
    if (basic != m_basic) {
        m_basic = basic;
        if (!m_bk11) {
            i_basic.change(basic ? 1 : 0);
            if (m_mapper != nullptr) m_mapper->routing_changed();
        }
    }
}

unsigned int A16M::get_value(unsigned int address)
{
    return read_byte(address, false);
}

unsigned int A16M::get_direct(unsigned int address)
{
    return read_byte(address, true);
}

// Слово по четному адресу не пересекает ни сегмент 4 КБ, ни границу 177000:
// источник у обоих байтов один
unsigned int A16M::get_value_word(unsigned int address)
{
    const int src = read_source(address);
    if (src == N) return _FFFF;
    const unsigned int in_segment = address & 07776;
    const uint8_t * p;
    if (src == R) p = m_rom_buf + in_segment;
    else if (m_ram_buf != nullptr) p = m_ram_buf + (src << 12) + in_segment;
    else return read_byte(address, false) | (read_byte(address + 1, false) << 8);
    return p[0] | (p[1] << 8);
}

void A16M::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    write_byte(address, value & 0xFF);
    if (address == OFF_REG) write_177130(value & 0xFF);
}

void A16M::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    const int src = write_source(address);
    if (src != N) {
        const unsigned int a = (src << 12) + (address & 07776);
        if (m_ram_buf != nullptr) {
            m_ram_buf[a] = (uint8_t)value;
            m_ram_buf[a + 1] = (uint8_t)(value >> 8);
        } else {
            m_ram->set_value(a, value & 0xFF);
            m_ram->set_value(a + 1, (value >> 8) & 0xFF);
        }
    }
    if (address == OFF_REG) write_177130(value);
}

void A16M::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("register", m_value, 16);
    w.b("strobe", m_strobe);
    w.b("read_off", m_rd_off);
    w.b("basic", m_basic);
}

emulator::Result A16M::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("register", m_value);
    r.b("strobe", m_strobe);
    r.b("read_off", m_rd_off);
    r.b("basic", m_basic);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> A16M::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"register", "Mode code last latched from 0177130",                         false});
    r.push_back({"mode",     "Memory mode: 160 Start, 60 Std10, 120 RAM10, 20 Basic, 140 Std11, 40 RAM11, 100 RAMzz, 0 Hlt11", false});
    r.push_back({"read_off", "1 while bit 2 keeps 0177130/0177132 from being read",         false});
    r.push_back({"basic",    "1 while bit 3 connects the BASIC ROM of the BK0010-01",       false});
    r.push_back({"layout",   "What each 4 KB segment of 0100000-0177777 shows to a read",   false});
    return r;
}

bool A16M::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "register") {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_value);
        return true;
    }
    if (field == "mode") {
        out.numeric = true;
        out.width = 8;
        out.values.push_back(m_value & 0160);
        return true;
    }
    if (field == "read_off") {
        out.numeric = true;
        out.values.push_back(m_rd_off ? 1 : 0);
        return true;
    }
    if (field == "basic") {
        out.numeric = true;
        out.values.push_back(m_basic ? 1 : 0);
        return true;
    }
    if (field == "layout") {
        // Одной строкой на сегмент 4 КБ (и отдельно на 177000-177777): что
        // там читается - сегмент ОЗУ платы, ее ПЗУ, память машины или ничего
        out.numeric = false;
        for (unsigned int s = 0; s < 9; s++) {
            const unsigned int offset = (s < 8) ? (s << 12) : OFF_HIGH;
            const unsigned int segment = (s < 8) ? s : 7;
            const int src = read_source(offset);
            std::string what;
            // ПЗУ БЕЙСИКа БК0010-01 занимает 120000-177377, и без разряда 3
            // его там нет
            const bool basic_area = !m_bk11 && segment >= 2 && s < 8;
            if (basic_area && m_basic) what = "machine (basic)";
            else if (src == R) what = "rom";
            else if (src == N) what = (base_disabled(segment) || basic_area) ? "none" : "machine";
            else what = "segment " + std::to_string(src);
            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + std::string(s < 8 ? "1" + std::to_string(s) + "0000" : "177000")
                      + ": " + what;
        }
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_a16m(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new A16M(im, cd);
}
