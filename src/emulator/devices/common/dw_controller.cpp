// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: What the DW hard disk controllers share: the ДВК board and the Электроника МС 0585 one

#include "dw_controller.h"
#include "emulator/utils.h"

// Накопитель 5 МБ: столько цилиндров DW.SYS ждёт при поднятом разряде 400 регистра
// состояния, а без него - вдвое больше
#define SMALL_CYLINDERS 153

DWController::DWController(InterfaceManager *im, EmulatorConfigDevice *cd, unsigned int window, unsigned int init_us):
      AddressableDevice(im, cd)
    , m_init_default(init_us)
{
    device_class = "hdd";
    m_clocked = true;
    can_read = true;
    can_write = true;
    addresable_size = window;
}

// Образ, без начальной установки: из снимка регистры и линии приходят сами
emulator::Result DWController::open_image(const std::string &file_name)
{
    std::string error;
    m_cylinders = 0;
    if (!m_image.open(file_name, error))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Hard disk image file not found")) + "} " + file_name);
    // Образ известного накопителя - его геометрия. Иначе цилиндры из конфигурации или
    // по размеру файла. Образ больше заданной геометрии допустим, хвост не используется;
    // меньше - только там, где он растёт
    unsigned int cylinders = m_cfg_cylinders;
    m_heads = m_cfg_heads;
    m_sectors = m_cfg_sectors;
    known_geometry(m_image.size(), m_heads, cylinders, m_sectors);
    const uint64_t track = (uint64_t)m_heads * m_sectors * HddImage::SECTOR_SIZE;
    const uint64_t image_cylinders = m_image.size() / track;
    m_cylinders = (cylinders != 0) ? cylinders : (unsigned int)image_cylinders;
    if (m_cylinders == 0 || m_cylinders > 1024 || (!m_grows_image && image_cylinders < m_cylinders)) {
        m_image.close();
        m_cylinders = 0;
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unrecognized hard disk geometry")) + "} " + file_name);
    }
    if (m_grows_image) m_image.extend(track * m_cylinders, 0);
    image_opened();
    return emulator::Result::ok();
}

// И при неудаче: гнездо могло опустеть
emulator::Result DWController::attach(const std::string &file_name)
{
    const emulator::Result res = open_image(file_name);
    init_controller();
    return res;
}

emulator::Result DWController::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    files = read_confg_value(cd, "files", false, std::string(""));
    m_image.set_write_protect(read_confg_value(cd, "write_protect", false, false));
    m_image.set_volatile(read_confg_value(cd, "volatile", false, false));

    m_heads = m_cfg_heads = read_confg_value(cd, "heads", false, (unsigned int)4);
    m_sectors = m_cfg_sectors = read_confg_value(cd, "sectors", false, (unsigned int)16);
    m_cfg_cylinders = read_confg_value(cd, "cylinders", false, (unsigned int)0);
    if (m_cfg_cylinders > 1024 || !geometry_ok())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Unsupported hard disk geometry")) + "} " + name);
    const std::string small_jumper = cd->get_parameter("small", false).value;
    m_small = small_jumper.empty() ? -1 : (parse_numeric_value(small_jumper) != 0 ? 1 : 0);

    // Времена в микросекундах: поиск и поворот до сектора, переход головок на
    // цилиндр, передача слова буфера, начальная установка
    m_seek_us = read_confg_value(cd, "seek_time", false, (unsigned int)2000);
    m_step_us = read_confg_value(cd, "step_time", false, (unsigned int)200);
    m_word_us = read_confg_value(cd, "word_time", false, (unsigned int)2);
    m_init_us = read_confg_value(cd, "init_time", false, m_init_default);
    return emulator::Result::ok();
}

// Образ на мегабайты возят отдельно от машины: его отсутствие не ошибка, гнездо
// пустое, образ вставляют командой load или из меню
emulator::Result DWController::load_image()
{
    const std::string image = read_confg_value(cd, "image", false, std::string(""));
    if (!image.empty()) {
        const std::string file = find_file_location(sd, image);
        if (!file.empty()) {
            const emulator::Result res = open_image(file);
            if (!res) return res;
        }
    }
    init_controller();
    return emulator::Result::ok();
}

void DWController::reset(MAYBE_UNUSED bool cold)
{
    init_controller();
}

bool DWController::small_drive() const
{
    return m_small > 0 || (m_small < 0 && m_cylinders != 0 && m_cylinders <= SMALL_CYLINDERS);
}

//--------------------------- Ход операции ----------------------------------//

void DWController::schedule(unsigned int event, unsigned int us)
{
    m_event = event;
    m_timeout = us;
    m_acc = 0;
}

void DWController::schedule_word()
{
    m_word_pending = true;
    m_word_timeout = m_word_us;
    m_word_acc = 0;
}

void DWController::adopt_word_event()
{
    m_event = EV_NONE;
    m_word_pending = true;
    m_word_timeout = m_timeout;
    m_word_acc = m_acc;
}

unsigned int DWController::seek_us(unsigned int cylinder) const
{
    const unsigned int d = (cylinder > m_track) ? cylinder - m_track : m_track - cylinder;
    return m_seek_us + d * m_step_us;
}

void DWController::clock(unsigned int counter)
{
    if (m_system_clock == 0) return;
    if (m_word_pending) {
        m_word_acc += (uint64_t)counter * 1000000ull;
        while (m_word_acc >= m_system_clock && m_word_timeout > 0) {
            m_word_acc -= m_system_clock;
            m_word_timeout--;
        }
        if (m_word_timeout == 0) {
            m_word_pending = false;
            word_ready();
        }
    }
    if (m_event == EV_NONE) return;

    m_acc += (uint64_t)counter * 1000000ull;
    const uint64_t step = m_system_clock;
    while (m_acc >= step && m_timeout > 0) {
        m_acc -= step;
        m_timeout--;
    }
    if (m_timeout > 0) return;

    const unsigned int event = m_event;
    m_event = EV_NONE;
    event_done(event);
}

//--------------------------- Регистры --------------------------------------//

void DWController::trace(unsigned int kind, unsigned int reg, unsigned int value)
{
    if (trace_skips(reg)) return;
    // Подряд одинаковые чтения (опрос готовности) - одной записью
    if (kind == 0 && m_trace_next > 0) {
        const TraceEntry &prev = m_trace[(m_trace_next - 1) & 1023];
        if (prev.kind == 0 && prev.reg == reg && prev.value == (value & 0xFFFF)) return;
    }
    TraceEntry &e = m_trace[m_trace_next & 1023];
    e.kind = (uint16_t)kind;
    e.reg = (uint16_t)reg;
    e.value = (uint16_t)value;
    m_trace_next++;
}

unsigned int DWController::get_value_word(unsigned int address)
{
    if (refused(address)) return 0xFFFF;
    const unsigned int reg = reg_of(address);
    const unsigned int v = read_reg(reg, false) & 0xFFFF;
    trace(0, reg, v);
    return v;
}

void DWController::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    const unsigned int reg = reg_of(address);
    trace(1, reg, value);
    write_reg(reg, value);
}

unsigned int DWController::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

// Байт пишется в свою половину регистра
void DWController::set_value(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    if (refused(address)) return;
    const unsigned int reg = reg_of(address);
    if ((address & 1) && odd_byte_write(reg)) return;
    const unsigned int old = read_reg(reg, true);
    const unsigned int w = (address & 1) ? ((old & 0x00FF) | ((value & 0xFF) << 8))
                                         : ((old & 0xFF00) | (value & 0xFF));
    trace(2, reg, w);
    write_reg(reg, w);
}

unsigned DWController::get_direct(unsigned address)
{
    const unsigned int w = read_reg(reg_of(address), true);
    return (address & 1) ? ((w >> 8) & 0xFF) : (w & 0xFF);
}

//--------------------------- Поля и команды --------------------------------//

ConfigFields DWController::get_config_fields()
{
    ConfigField f;
    f.name = "image";
    f.title = QT_TRANSLATE_NOOP("ConfigFields", "Hard disk image");
    f.type = CONFIG_FIELD_FILE;
    f.files = cd->get_parameter("files", false).value;
    f.embeddable = false;

    ConfigField v;
    v.name = "volatile";
    v.title = QT_TRANSLATE_NOOP("ConfigFields", "Hard disk writes");
    v.type = CONFIG_FIELD_CHOICE;
    v.def = "0";
    v.values.push_back({"0", QT_TRANSLATE_NOOP("ConfigFields", "Into the image file")});
    v.values.push_back({"1", QT_TRANSLATE_NOOP("ConfigFields", "Into memory only, the image stays as it was")});
    return {f, v};
}

std::vector<DeviceFieldInfo> DWController::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"attached",  "1, когда образ вставлен",                   false});
    r.push_back({"file",      "Имя образа",                                false});
    r.push_back({"protected", "1, когда запись в образ запрещена",          false});
    r.push_back({"volatile",  "1, когда запись идёт в память, а не в файл", false});
    r.push_back({"geometry",  "Цилиндры, головки и секторы образа",         false});
    r.push_back({"csr",       "Регистр 174016",                            false});
    r.push_back({"err",       "Регистр ошибок 174004",                     false});
    r.push_back({"command",   "Последняя команда",                         false});
    r.push_back({"cylinder",  "Цилиндр в регистре",                        false});
    r.push_back({"head",      "Головка в регистре",                        false});
    r.push_back({"sector",    "Сектор в регистре",                         false});
    r.push_back({"reads",     "Сколько секторов прочитано",                 false});
    r.push_back({"writes",    "Сколько секторов записано",                  false});
    r.push_back({"trace",     "Последние обращения к регистрам: вид (0 чтение, 1 запись слова, 2 байта), регистр, значение", true});
    return r;
}

std::vector<DeviceCommandInfo> DWController::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"load",     "\"file\"", "Вставляет образ винчестера"});
    r.push_back({"eject",    "",         "Вынимает образ"});
    r.push_back({"protect",  "[0|1]",    "Запрещает или разрешает запись в образ"});
    r.push_back({"volatile", "[0|1]",    "Запись в память вместо файла; выключение забывает записанное"});
    return r;
}

bool DWController::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "file") { out.text = m_image.file_name(); return true; }
    // Отладка: последние обращения, старые первыми, по три числа на каждое
    if (field == "trace") {
        if (to >= 1024) to = 1023;
        out.numeric = true;
        out.has_start = true;
        out.start = from;
        out.width = 16;
        for (unsigned int i = from; i <= to; i++) {
            const TraceEntry &e = m_trace[(m_trace_next + i) & 1023];
            out.values.push_back(e.kind);
            out.values.push_back(e.reg);
            out.values.push_back(e.value);
        }
        return true;
    }
    if (field == "geometry") {
        out.text = std::to_string(m_cylinders) + "/" + std::to_string(m_heads) + "/" + std::to_string(m_sectors);
        return true;
    }
    out.numeric = true;
    out.width = 16;
    if (field == "attached")  { out.values.push_back(m_image.attached() ? 1 : 0);     return true; }
    if (field == "protected") { out.values.push_back(m_image.is_protected() ? 1 : 0); return true; }
    if (field == "volatile")  { out.values.push_back(m_image.is_volatile() ? 1 : 0);  return true; }
    if (field == "csr")       { out.values.push_back(m_csr);      return true; }
    if (field == "err")       { out.values.push_back(m_err);      return true; }
    if (field == "command")   { out.values.push_back(m_command);  return true; }
    if (field == "cylinder")  { out.values.push_back(m_cyl);      return true; }
    if (field == "head")      { out.values.push_back(m_head);     return true; }
    if (field == "sector")    { out.values.push_back(m_sector);   return true; }
    out.width = 32;
    if (field == "reads")     { out.values.push_back(m_reads);    return true; }
    if (field == "writes")    { out.values.push_back(m_writes);   return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

emulator::Result DWController::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "load") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{DVKDW|" + std::string(QT_TRANSLATE_NOOP("DVKDW", "Command 'load' expects a file name")) + "}");
        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];
        return attach(file);
    }
    if (command == "eject") {
        m_image.close();
        m_cylinders = 0;
        init_controller();
        return emulator::Result::ok();
    }
    if (command == "protect") {
        m_image.set_write_protect((p.empty() || p[0].empty()) ? !m_image.write_protect()
                                                               : parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }
    if (command == "volatile") {
        m_image.set_volatile((p.empty() || p[0].empty()) ? !m_image.is_volatile()
                                                          : parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }
    return AddressableDevice::send_command(command, parameters);
}

void DWController::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("err", m_err);
    w.u("sector", m_sector);
    w.u("cyl", m_cyl);
    w.u("head", m_head);
    w.u("csr", m_csr);
    w.n("track", m_track);
    w.u("command", m_command);
    w.array("buf", m_buf, 256);
    w.n("event", m_event);
    w.n64("timeout", m_timeout);
    w.n64("acc", m_acc);
    w.b("word_pending", m_word_pending);
    w.n64("word_timeout", m_word_timeout);
    w.n64("word_acc", m_word_acc);
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.b("write_protect", m_image.write_protect());
    if (m_image.attached()) {
        w.b("attached", true);
        m_image.save_state(w, "image", name);
    }
}

emulator::Result DWController::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;

    // Образ - первым: регистры ниже - уже его. Без начальной установки: линии
    // восстановлены
    bool attached = false;
    if (r.b("attached", attached) && attached) {
        std::string file;
        if (r.s("image", file) && !file.empty()) {
            const std::string path = find_file_location(sd, file);
            if (path.empty())
                return emulator::Result::error(emulator::ErrorCode::FileError,
                    "{MachineState|Saved state} " + name + ": file not found: " + file);
            res = open_image(path);
            if (!res) return res;
            // Копия из снимка и его ОЗУ сняты вместе: запись в файл их
            // рассогласовала бы, поэтому восстановленный диск пишет в память
            m_image.set_volatile(true);
        }
    }

    r.u("err", m_err);
    r.u("sector", m_sector);
    r.u("cyl", m_cyl);
    r.u("head", m_head);
    r.u("csr", m_csr);
    r.u("track", m_track);
    r.u("command", m_command);
    r.array("buf", m_buf, 256);
    r.u("event", m_event);
    r.n64("timeout", m_timeout);
    r.n64("acc", m_acc);
    r.b("word_pending", m_word_pending);
    r.n64("word_timeout", m_word_timeout);
    r.n64("word_acc", m_word_acc);
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    bool wp = false;
    if (r.b("write_protect", wp)) m_image.set_write_protect(wp);
    return emulator::Result::ok();
}
