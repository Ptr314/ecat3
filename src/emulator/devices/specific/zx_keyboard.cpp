// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Клавиатура ZX Spectrum поверх клавиатуры машины

#include "emulator/utils.h"
#include "emulator/devices/common/keyboard.h"
#include "zx_keyboard.h"

ZXKeyboard::ZXKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_port(this, im, 16, "port", MODE_R)
{
    addresable_size = 1;
    can_write = false;
    set_default_matrix();
}

// Матрица ZX Spectrum: полуряд выбирается разрядом адреса A8+N, разряды 0-4
// ответа идут в том порядке, в каком клавиши перечислены здесь.
//
// Раскладка взята из руководства к машине: сорокаклавишная сетка Спектрума
// положена на клавиатуру Арго по месту, а буквы в ней подписаны по-русски.
// Ряд QWERTYUIOP - это ЙЦУКЕНГШЩЗ, ряд ASDFGHJKL - ФЫВАПРОЛД, ряд ZXCVBNM -
// ЯЧСМИТЬ; у Арго это те же клавиши, потому что коды им ПЗУ дает латинские.
//
// А вот три служебные клавиши оказались не там, где их ждешь:
//   CAPS SHIFT   - это ДОП, а не тот шифт, что в матрице Арго;
//   SYMBOL SHIFT - клавиша Б, у Арго она дает запятую;
//   SPACE (BREAK)- клавиша Ю, у Арго она дает точку.
// Обе последние стоят в нижнем ряду там же, где у Спектрума его SYMBOL SHIFT и
// пробел, так что наложение по месту сходится и здесь. Собственный пробел Арго
// и его шифт в режиме ZX не делают ничего
void ZXKeyboard::set_default_matrix()
{
    static const char * m[8][5] = {
        {"key_dop",     "key_z",     "key_x",     "key_c",     "key_v"},
        {"key_a",       "key_s",     "key_d",     "key_f",     "key_g"},
        {"key_q",       "key_w",     "key_e",     "key_r",     "key_t"},
        {"key_1",       "key_2",     "key_3",     "key_4",     "key_5"},
        {"key_0",       "key_9",     "key_8",     "key_7",     "key_6"},
        {"key_p",       "key_o",     "key_i",     "key_u",     "key_y"},
        {"key_enter",   "key_l",     "key_k",     "key_j",     "key_h"},
        {"key_dot",     "key_comma", "key_m",     "key_n",     "key_b"}
    };
    for (unsigned int r = 0; r < 8; r++)
        for (unsigned int b = 0; b < 5; b++)
            m_matrix[r][b] = m[r][b];
}

emulator::Result ZXKeyboard::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    const std::string name_kbd = read_confg_value(cd, "keyboard", false, std::string("keyboard"));
    Source = dynamic_cast<Keyboard*>(im->dm->get_device_by_name(name_kbd));
    if (Source == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ZXKeyboard|" + std::string(QT_TRANSLATE_NOOP("ZXKeyboard", "A keyboard device is expected")) + "} " + name);

    return emulator::Result::ok();
}

// Ноль в разряде адреса выбирает полуряд, ноль в ответе - нажатую клавишу.
// Выбранных полурядов может быть несколько, и тогда ответ - их И
unsigned int ZXKeyboard::get_value(MAYBE_UNUSED unsigned int address)
{
    const unsigned int sel = (i_port.value >> 8) & 0xFF;
    unsigned int res = 0x1F;

    const std::vector<std::string> held = Source->ids_held();
    if (!held.empty())
    {
        for (unsigned int r = 0; r < 8; r++)
        {
            if (((sel >> r) & 1) != 0) continue;
            for (unsigned int b = 0; b < 5; b++)
            {
                const std::string & id = m_matrix[r][b];
                if (id.empty()) continue;
                for (size_t i = 0; i < held.size(); i++)
                    if (held[i] == id) { res &= ~(1u << b); break; }
            }
        }
    }

    //Разряд 6 - вход магнитофона, разряды 5 и 7 не подключены: на живой машине
    //они читаются единицами
    m_last = res | 0xE0;
    return m_last;
}

unsigned int ZXKeyboard::get_direct(unsigned int address)
{
    return get_value(address);
}

void ZXKeyboard::set_value(MAYBE_UNUSED unsigned int address, MAYBE_UNUSED unsigned int value, MAYBE_UNUSED bool force)
{
    //Запись в этот порт у Спектрума - цвет бордюра и динамик. У Арго и то и
    //другое на своем порту ($80), а как они связаны в режиме ZX, со схемы не
    //выяснить, поэтому запись здесь не делает ничего
}

std::vector<DeviceFieldInfo> ZXKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"rows", "All eight half-rows as the machine would read them", false});
    return r;
}

bool ZXKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "rows")
    {
        out.numeric = true; out.width = 8;
        const std::vector<std::string> held = Source?Source->ids_held():std::vector<std::string>();
        for (unsigned int r = 0; r < 8; r++)
        {
            unsigned int v = 0x1F;
            for (unsigned int b = 0; b < 5; b++)
                for (size_t i = 0; i < held.size(); i++)
                    if (held[i] == m_matrix[r][b]) { v &= ~(1u << b); break; }
            out.values.push_back(v | 0xE0);
        }
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_zx_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new ZXKeyboard(im, cd);
}
