// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A block device on an image file, for a controller that keeps several of them

#include "hdd_unit.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"

HddUnit::HddUnit(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
    device_class = "hdd";
}

emulator::Result HddUnit::load_image(const std::string &file_name)
{
    // Запись в память: файл открывается только на чтение
    std::string error;
    if (!m_image.open(file_name, error, m_image.is_volatile()))
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{HddUnit|" + std::string(QT_TRANSLATE_NOOP("HddUnit", "Disk image file not found")) + "} " + file_name);
    return emulator::Result::ok();
}

emulator::Result HddUnit::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    m_files = cd->get_parameter("files", false).value;   // подпись окна выбора, как написана
    m_image.set_write_protect(read_confg_value(cd, "write_protect", false, false));
    m_image.set_volatile(read_confg_value(cd, "volatile", false, false));
    // Образа может и не быть: гнездо пустое, образ вставляют командой load
    // Как написано: read_confg_value() приводит строку к нижнему регистру, и
    // образ с заглавными буквами в имени не находился там, где регистр
    // в именах файлов важен (Linux, веб-версия)
    const std::string image = str_trim(cd->get_parameter("image", false).value);
    if (!image.empty()) {
        const std::string file = find_file_location(sd, image);
        if (!file.empty()) {
            res = load_image(file);
            if (!res) return res;
        }
    }
    return emulator::Result::ok();
}

bool HddUnit::read_block(uint64_t block, uint8_t * buffer)
{
    if (!m_image.attached() || block >= blocks()) return false;
    if (!m_image.read(block * BLOCK_SIZE, buffer)) return false;
    m_reads++;
    return true;
}

bool HddUnit::write_block(uint64_t block, const uint8_t * buffer)
{
    if (!m_image.attached() || is_protected() || block >= blocks()) return false;
    if (!m_image.write(block * BLOCK_SIZE, buffer)) return false;
    m_writes++;
    return true;
}

ConfigFields HddUnit::get_config_fields()
{
    // Только ссылка: образ пишется по ходу работы, и распакованная из
    // конфигурации копия потеряла бы записанное
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

void HddUnit::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.b("write_protect", m_image.write_protect());
    if (!m_image.attached()) return;
    w.b("attached", true);
    m_image.save_state(w, "image", name);
}

emulator::Result HddUnit::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    m_image.close();
    bool attached = false;
    if (r.b("attached", attached) && attached) {
        std::string file;
        if (r.s("image", file) && !file.empty()) {
            const std::string path = find_file_location(sd, file);
            if (path.empty())
                return emulator::Result::error(emulator::ErrorCode::FileError,
                    "{MachineState|Saved state} " + name + ": file not found: " + file);
            res = load_image(path);
            if (!res) return res;
            // Копия образа в снимке пишется только в память, как у
            // винчестера УК-НЦ: иначе файл и память снимка разойдутся
            m_image.set_volatile(true);
        }
    }
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    bool write_protect = false;
    if (r.b("write_protect", write_protect)) m_image.set_write_protect(write_protect);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> HddUnit::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"attached",  "1 when an image is inserted",                  false});
    r.push_back({"file",      "File name of the image, without the path",      false});
    r.push_back({"path",      "Full path of the image",                         false});
    r.push_back({"blocks",    "Size in 512-byte blocks",                       false});
    r.push_back({"protected", "1 when writes are refused",                     false});
    r.push_back({"volatile",  "1 when writes go to memory, not to the file",   false});
    r.push_back({"reads",     "Blocks read",                                    false});
    r.push_back({"writes",    "Blocks written",                                 false});
    return r;
}

bool HddUnit::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    // Как у дисковода: имя - для сценариев, которые не зависят от каталога
    // (образ из архива распаковывается во временный), путь - отдельно
    if (field == "file") {
        out.text = m_image.attached() ? dsk_tools::get_filename(m_image.file_name()) : std::string();
        return true;
    }
    if (field == "path") {
        out.text = m_image.file_name();
        return true;
    }
    out.numeric = true;
    out.width = 32;
    if (field == "attached")  { out.values.push_back(m_image.attached() ? 1 : 0); return true; }
    if (field == "blocks")    { out.values.push_back((unsigned int)blocks()); return true; }
    if (field == "protected") { out.values.push_back(m_image.is_protected() ? 1 : 0); return true; }
    if (field == "volatile")  { out.values.push_back(m_image.is_volatile() ? 1 : 0); return true; }
    if (field == "reads")     { out.values.push_back(m_reads); return true; }
    if (field == "writes")    { out.values.push_back(m_writes); return true; }
    out.numeric = false;
    out.width = 0;
    return ComputerDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> HddUnit::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"load",     "\"file\"", "Inserts a disk image"});
    r.push_back({"eject",    "",         "Removes the image"});
    r.push_back({"protect",  "[0|1]",    "Refuses or allows writes to the image"});
    r.push_back({"volatile", "[0|1]",    "Writes into memory instead of the file; switching off forgets them"});
    return r;
}

emulator::Result HddUnit::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    if (command == "load") {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{HddUnit|" + std::string(QT_TRANSLATE_NOOP("HddUnit", "Command 'load' expects a file name")) + "}");
        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];
        return load_image(file);
    }
    if (command == "eject") {
        m_image.close();
        return emulator::Result::ok();
    }
    if (command == "protect") {
        m_image.set_write_protect((p.empty() || p[0].empty()) ? !m_image.write_protect()
                                                               : parse_numeric_value(p[0], 10) != 0);
        return emulator::Result::ok();
    }
    if (command == "volatile") {
        const bool on = (p.empty() || p[0].empty()) ? !m_image.is_volatile()
                                                     : parse_numeric_value(p[0], 10) != 0;
        const bool reopen = !on && m_image.is_volatile() && m_image.attached();
        m_image.set_volatile(on);
        // Образ был открыт только на чтение ради записи в память
        if (reopen) return load_image(m_image.file_name());
        return emulator::Result::ok();
    }
    return ComputerDevice::send_command(command, parameters);
}

ComputerDevice * create_hdd_unit(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new HddUnit(im, cd);
}
