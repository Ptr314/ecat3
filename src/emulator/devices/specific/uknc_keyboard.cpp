// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ keyboard (МС7007)

#include "uknc_keyboard.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"

#define KEY_RELEASED    0200        // разряд 7 кода означает отпускание

#define CALLBACK_READ   1           // чтение регистра кода
#define CALLBACK_ENABLE 2           // разряд разрешения прерывания

UKNCKeyboard::UKNCKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      Keyboard(im, cd)
    , i_ready(this, im, 1, "ready", MODE_W)
    , i_pressed(this, im, 1, "pressed", MODE_W)
    , i_read(this, im, 1, "read", MODE_R, CALLBACK_READ)
    , i_irq_enable(this, im, 1, "irq_enable", MODE_R, CALLBACK_ENABLE)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_ENABLE)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
{
}

emulator::Result UKNCKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = Keyboard::load_config(sd);
    if (!res) return res;

    m_vector = read_confg_value(cd, "vector", false, (unsigned int)0300);

    // Регистр, в который кладётся код клавиши - на машине это 177702
    const std::string port_name = read_confg_value(cd, "port-value", false, std::string(""));
    if (!port_name.empty())
        m_port = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(port_name, false));
    if (m_port == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "A port to put the key code into is expected")) + "} " + name);

    // Раскладка: имя клавиши хоста -> номер клавиши машины. Модификаторов
    // здесь нет и быть не может - регистровые клавиши имеют свои номера,
    // а перекодировкой занимается ПЗУ машины
    const std::string map_file = find_file_location(sd, cd->get_parameter("map", false).value);
    if (map_file.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Keyboard map file is expected")) + "} " + name);

    const std::string content = dsk_tools::utf8_read_file(map_file);
    if (content.empty())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Error reading map file")) + "} " + map_file);

    const std::vector<std::string> lines = split_string(content, '\n', true);
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = str_trim(lines[i]);
        const size_t comment = line.find("//");
        if (comment != std::string::npos) line = str_trim(line.substr(0, comment));
        if (line.empty()) continue;

        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Map file entry is incorrect")) + "} " + line);

        const std::string key_name = str_trim(line.substr(0, colon));
        const std::string value = str_trim(line.substr(colon + 1));

        const unsigned int host = translate_key(key_name);
        if (host == _FFFF)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Unknown key in the map file")) + "} " + line);

        unsigned int scan;
        try {
            scan = parse_numeric_value(value);
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Invalid value in the map file")) + "} " + line);
        }

        KeyEntry e;
        e.host = host;
        e.scan = scan & 0177;
        m_keys.push_back(e);
    }

    m_ready = false;
    i_ready.change(0);
    i_pressed.change(0);
    update_irq();

    return emulator::Result::ok();
}

void UKNCKeyboard::reset(const bool cold)
{
    Keyboard::reset(cold);
    m_codes = 0;
    m_last = 0;
    m_ready = false;
    m_offered = 0;
    i_ready.change(0);
    i_pressed.change(0);
    update_irq();
}

unsigned int UKNCKeyboard::scan_of(unsigned int host) const
{
    for (size_t i = 0; i < m_keys.size(); i++)
        if (m_keys[i].host == host) return m_keys[i].scan;
    return _FFFF;
}

void UKNCKeyboard::send(unsigned int scan, bool press)
{
    // Отпускание отличается от нажатия только разрядом 7
    const unsigned int code = (scan & 0177) | (press? 0 : KEY_RELEASED);

    m_last = code;
    m_codes++;

    // Код кладётся в регистр принудительно: это выход контроллера, а не то,
    // что записала программа
    m_port->set_value_word(0, code, true);

    // Готовность стоит, пока программа не прочитает регистр кода
    m_ready = true;
    i_ready.change(1);
    i_pressed.change(press? 1 : 0);
    update_irq();
}

// Раскладка самой машины: слева имя клавиши МС7007, справа её номер. Никаких
// пометок регистра тут нет и быть не может - регистровые клавиши у этой
// машины имеют собственные номера, а в символы всё переводит её ПЗУ. Пустые
// строки и комментарии базовый класс уже убрал.
emulator::Result UKNCKeyboard::parse_key_table(const std::vector<std::string> &body, const std::string &file)
{
    for (size_t i = 0; i < body.size(); i++)
    {
        const size_t colon = body[i].find(':');
        if (colon == std::string::npos)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Key table entry is incorrect")) + "} " + body[i] + " (" + file + ")");

        IdEntry e;
        e.id = str_trim(body[i].substr(0, colon));
        try {
            e.scan = parse_numeric_value(str_trim(body[i].substr(colon + 1))) & 0177;
        } catch (const std::exception &) {
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{UKNCKeyboard|" + std::string(QT_TRANSLATE_NOOP("UKNCKeyboard", "Invalid value in the key table")) + "} " + body[i] + " (" + file + ")");
        }

        // Обе клавиши НР дают один номер, поэтому имя, объявленное в заголовке
        // как регистровое, встречается тут ещё раз - за номером. register_key_id()
        // это переживает: роль уже записана, второй раз имя не добавляется
        m_ids.push_back(e);
        register_key_id(e.id);
    }
    return emulator::Result::ok();
}

// Клавиша нажата на рисунке. Здесь нет ни перекодировки, ни регистра: имя
// уже принадлежит машине, а номер уходит ей как есть - и при нажатии, и при
// отпускании, разрядом 7
void UKNCKeyboard::send_key_id(const std::string &id, bool press)
{
    for (size_t i = 0; i < m_ids.size(); i++)
        if (m_ids[i].id == id) {
            send(m_ids[i].scan, press);
            return;
        }
}

void UKNCKeyboard::update_irq()
{
    // Прерывание от клавиатуры разрешено разрядом 6 регистра 177700. У БК
    // одноимённый разряд, наоборот, запрещает - здесь единица разрешает
    const bool enabled = (i_irq_enable.value & 1) != 0;

    unsigned int vector = 0;
    if (m_ready && enabled)              vector = m_vector;
    else if ((i_virq_in.value & 1) == 0) vector = i_vector_in.value & 0xFFFF;

    // Как у каналов и таймера: запрос берётся по фронту, поэтому при смене
    // источника линию надо отпустить и прижать заново
    if (vector != m_offered) {
        if (vector != 0) {
            i_vector.change(vector);
            i_virq.change(1);
            i_virq.change(0);
        } else
            i_virq.change(1);
        m_offered = vector;
    }
}

void UKNCKeyboard::interface_callback(unsigned int callback_id, MAYBE_UNUSED unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_READ) {
        // Чтение регистра кода снимает готовность
        m_ready = false;
        i_ready.change(0);
    }
    update_irq();
}

void UKNCKeyboard::key_down(unsigned int key)
{
    const unsigned int scan = scan_of(key);
    if (scan == _FFFF) return;
    send(scan, true);
}

void UKNCKeyboard::key_up(unsigned int key)
{
    const unsigned int scan = scan_of(key);
    if (scan == _FFFF) return;
    send(scan, false);
}

std::vector<DeviceFieldInfo> UKNCKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = Keyboard::get_device_fields();
    r.push_back({"codes", "Сколько кодов отдано машине с пуска",       false});
    r.push_back({"last",  "Последний отданный код, разряд 7 - отпускание", false});
    r.push_back({"keys",  "Сколько клавиш в раскладке хоста",          false});
    r.push_back({"ids",   "Сколько клавиш у самой машины (рисунок)",   false});
    return r;
}

bool UKNCKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    out.width = 8;
    if (field == "codes") { out.values.push_back(m_codes); return true; }
    if (field == "last")  { out.values.push_back(m_last);  return true; }
    if (field == "keys")  { out.values.push_back((unsigned int)m_keys.size()); return true; }
    if (field == "ids")   { out.values.push_back((unsigned int)m_ids.size());  return true; }
    out.numeric = false;
    out.width = 0;
    return Keyboard::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCKeyboard(im, cd);
}
