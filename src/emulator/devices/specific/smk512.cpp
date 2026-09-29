// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: СМК-512 memory board of the БК (AltPro)

#include "smk512.h"
#include "emulator/utils.h"

// Смещения внутри окна 100000-177777
static const unsigned int OFF_REG     = 077130;     // 177130, регистр режима и дисковода
static const unsigned int OFF_FDC_END = 077133;
static const unsigned int OFF_HDD     = 077740;     // 177740-177757, винчестер платы
static const unsigned int OFF_HDD_END = 077757;
static const unsigned int OFF_HIGH    = 077000;     // 177000: верхняя часть сегмента 7

// Номер режима - разряды 4-6 значения, то есть код режима / 020
enum {
    M_HLT11 = 0,    // 000
    M_ALL   = 1,    // 020
    M_RAM11 = 2,    // 040
    M_STD10 = 3,    // 060
    M_HLT10 = 4,    // 100
    M_RAM10 = 5,    // 120
    M_STD11 = 6,    // 140
    M_SYS   = 7     // 160
};

static const int N = -1;    // сегмент платой не занят
static const int R = 8;     // ПЗУ платы

// Что стоит в сегментах 100000..170000 (до 176777) в каждом режиме: номер
// сегмента текущей страницы, ПЗУ или ничего. Таблица режимов АльтПро (сводка
// документации, maxiol.com t5563) - и таблицы ожидаемых кодов теста
// контроллеров gid v4.77F, с которыми она сходится для реплики
static const int SEGMENTS[8][8] = {
    //  100000 110000 120000 130000 140000 150000 160000 170000
    {   N,     N,     N,     N,     4,     5,     6,     7 },    // 000 Hlt11
    {   4,     5,     6,     7,     0,     1,     2,     3 },    // 020 All
    {   N,     N,     N,     N,     4,     5,     6,     7 },    // 040 ОЗУ11
    {   N,     N,     2,     3,     4,     5,     R,     7 },    // 060 Std10
    {   0,     1,     2,     3,     4,     5,     6,     7 },    // 100 Hlt10, сегмент 0 только на чтение
    {   0,     1,     2,     3,     4,     5,     6,     7 },    // 120 ОЗУ10
    {   N,     N,     N,     N,     N,     N,     R,     7 },    // 140 Std11
    {   N,     N,     6,     7,     0,     1,     R,     R }     // 160 SYS
};

// Сигналы, которыми плата отключает память самой машины (разъем МПИ):
// монитор БК0010 (100000-117777), ПЗУ БОС БК0011М (140000-157777; нужна
// доработка по контакту B6) и верхнее ОЗУ БК0011М (окно 100000-137777)
static const bool MON10_OFF[8] = { true,  true,  false, false, true,  true,  false, false };
static const bool MON11_OFF[8] = { true,  true,  true,  true,  false, false, false, false };
static const bool UP11_OFF[8]  = { false, true,  false, true,  false, true,  false, true  };

SMK512::SMK512(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    addresable_size = 040000 * 2;
    can_read = true;
    can_write = true;
}

emulator::Result SMK512::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    m_ram = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
    m_rom = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->get_parameter("rom").value));
    if (m_ram == nullptr || m_rom == nullptr || m_ram->get_size() < 512*1024 || m_rom->get_size() < 4096)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);

    const std::string machine = cd->get_parameter("machine", false).value;
    m_bk11 = (machine == "bk11");

    return emulator::Result::ok();
}

void SMK512::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_value = 0160;         // SYS, страница 0: с него БК и стартует
    m_strobe = false;
    m_rd_off = false;
}

// Номер первого сегмента 4 КБ страницы: разряды 10, 2, 3, 0 значения
unsigned int SMK512::page_base() const
{
    unsigned int p = 0;
    if (m_value & 02000) p += 010;
    if (m_value & 4)     p += 020;
    if (m_value & 010)   p += 040;
    if (m_value & 1)     p += 0100;
    return p;
}

// Источник байта по смещению в окне, для чтения: номер сегмента страницы, R или N.
// Верхняя часть сегмента 7 (177000-177777) читается из платы только в SYS
// (ПЗУ) и в All (ОЗУ, перекрывая регистры)
int SMK512::source(unsigned int offset) const
{
    const unsigned int m = mode_index();
    if (offset >= OFF_HIGH) {
        if (m == M_SYS) return R;
        if (m == M_ALL) return 3;
        return N;
    }
    return SEGMENTS[m][offset >> 12];
}

// Отключена ли сигналом платы собственная память машины в этом сегменте
bool SMK512::base_disabled(unsigned int segment) const
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

unsigned int SMK512::route(unsigned int address, unsigned int mode)
{
    const unsigned int m = mode_index();

    // Регистры самой платы: дисковод и винчестер отвечают сами, а запись в
    // 177130 плата еще и защелкивает
    if (address >= OFF_REG && address <= OFF_FDC_END) {
        if (mode == MODE_W) return ROUTE_THROUGH;
        if (!m_rd_off) return ROUTE_PASS;
        // Чтение запрещено разрядом 2: отвечает то, что плата ставит на эти
        // адреса, а если ничего - никто
        return (source(address) != N) ? ROUTE_ANSWER : ROUTE_TIMEOUT;
    }
    if (address >= OFF_HDD && address <= OFF_HDD_END) return ROUTE_PASS;

    // Над страницей ввода-вывода плата отвечает вместе с регистрами машины, и
    // на магистрали их ответы складываются: конфликт читается единицей. Так
    // тест gid опрашивает клавиатуру (разряд 6 0177716) в режиме All, где
    // под регистром лежит его синхрокод с нулем в этом разряде
    if (address >= OFF_HIGH) {
        if (mode == MODE_W)
            return (m == M_HLT10 || m == M_HLT11) ? ROUTE_THROUGH : ROUTE_PASS;
        return (source(address) != N) ? ROUTE_OR : ROUTE_PASS;
    }

    const unsigned int segment = address >> 12;
    const int src = SEGMENTS[m][segment];
    if (src == N)
        return base_disabled(segment) ? ROUTE_TIMEOUT : ROUTE_PASS;
    if (mode != MODE_W) return ROUTE_ANSWER;
    if (src == R) return ROUTE_TIMEOUT;
    // Сегмент 0 в Hlt10 защищен от записи: ее принимает ОЗУ машины, если оно
    // там есть (БК0011М), а если нет - ответа нет
    if (m == M_HLT10 && segment == 0)
        return base_disabled(segment) ? ROUTE_TIMEOUT : ROUTE_PASS;
    // Запись видят все: ОЗУ машины под платой тоже ее получает
    return (!base_disabled(segment) && m_bk11 && segment < 4) ? ROUTE_THROUGH : ROUTE_ANSWER;
}

unsigned int SMK512::read_byte(unsigned int offset, bool direct)
{
    const int src = source(offset);
    if (src == N) return _FFFF;
    const unsigned int in_segment = offset & 07777;
    if (src == R)
        return direct ? m_rom->get_direct(in_segment) : m_rom->get_value(in_segment);
    const unsigned int a = ((page_base() + src) << 12) + in_segment;
    return direct ? m_ram->get_direct(a) : m_ram->get_value(a);
}

void SMK512::write_byte(unsigned int offset, unsigned int value)
{
    const unsigned int m = mode_index();
    int src;
    if (offset >= OFF_HIGH)
        src = (m == M_HLT10 || m == M_HLT11) ? 7 : N;
    else
        src = SEGMENTS[m][offset >> 12];
    if (src == N || src == R) return;
    if (m == M_HLT10 && offset < 010000) return;
    m_ram->set_value(((page_base() + src) << 12) + (offset & 07777), value);
}

// Запись в 177130. Реплика защелкивает значение записью, которая идет сразу
// за стробом - младшей тетрадой ровно 0110 (оригинал смотрел только на разряды
// 1 и 2). Разряд 2 каждой записи запрещает чтение 177130/177132
void SMK512::write_177130(unsigned int value)
{
    const bool strobe = (value & 017) == 06;
    if (m_strobe && !strobe) m_value = value & 0177777;
    m_strobe = strobe;
    m_rd_off = (value & 4) != 0;
}

unsigned int SMK512::get_value(unsigned int address)
{
    return read_byte(address, false);
}

unsigned int SMK512::get_direct(unsigned int address)
{
    return read_byte(address, true);
}

void SMK512::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    // В режимах Hlt запись в 177000-177777 ложится и в ОЗУ - по той раскладке,
    // которая стояла до нее
    write_byte(address, value & 0xFF);
    if (address == OFF_REG) write_177130(value & 0xFF);
}

void SMK512::set_value_word(unsigned int address, unsigned int value, bool force)
{
    (void)force;
    write_byte(address, value & 0xFF);
    write_byte(address + 1, (value >> 8) & 0xFF);
    if (address == OFF_REG) write_177130(value);
}

void SMK512::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("register", m_value, 16);
    w.b("strobe", m_strobe);
    w.b("read_off", m_rd_off);
}

emulator::Result SMK512::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("register", m_value);
    r.b("strobe", m_strobe);
    r.b("read_off", m_rd_off);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> SMK512::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"register", "Mode and page last latched from 0177130",                 false});
    r.push_back({"mode",     "Memory mode: 160 SYS, 60 Std10, 120 RAM10, 20 All, 140 Std11, 40 RAM11, 100 Hlt10, 0 Hlt11", false});
    r.push_back({"page",     "Number of the 32 KB page, 0-15",                          false});
    r.push_back({"read_off", "1 while bit 2 keeps 0177130/0177132 from being read",     false});
    r.push_back({"layout",   "What each 4 KB segment of 0100000-0177777 shows",         false});
    return r;
}

bool SMK512::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
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
    if (field == "page") {
        out.numeric = true;
        out.values.push_back(page_base() >> 3);
        return true;
    }
    if (field == "read_off") {
        out.numeric = true;
        out.values.push_back(m_rd_off ? 1 : 0);
        return true;
    }
    if (field == "layout") {
        // Одной строкой на сегмент 4 КБ (и отдельно на 177000-177777): что
        // там читается - страница и сегмент памяти платы, ее ПЗУ, память
        // машины или ничего
        out.numeric = false;
        const unsigned int base = page_base();
        for (unsigned int s = 0; s < 9; s++) {
            const int src = (s < 8) ? SEGMENTS[mode_index()][s] : source(OFF_HIGH);
            std::string what;
            if (src == R) what = "rom";
            else if (src == N) what = base_disabled(s < 8 ? s : 7) ? "none" : "machine";
            else what = "page " + std::to_string((base + src) >> 3) + " segment " + std::to_string((base + src) & 7);
            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + std::string(s < 8 ? "1" + std::to_string(s) + "0000" : "177000")
                      + ": " + what;
        }
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_smk512(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new SMK512(im, cd);
}
