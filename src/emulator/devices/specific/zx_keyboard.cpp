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
    , i_ear(this, im, 1, "ear", MODE_R)
    , i_control(this, im, 8, "control", MODE_W)
{
    //Клавиши опрашиваются по времени, а не при чтении порта: иначе удержание
    //Ф10 переключало бы сканирование без конца
    m_clocked = true;
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

    //Сканирование выключено - клавиатуры для Спектрума просто нет. Вход
    //магнитофона при этом остаётся: он не часть опроса клавиш
    if (!m_scan)
    {
        m_last = res | 0xA0 | (ear_level()?0x40:0);
        return m_last;
    }

    //Матрица уже собрана опросом клавиш, здесь остается только И выбранных
    //полурядов - за это и платит цикл ПЗУ, читающий порт без передышки
    for (unsigned int r = 0; r < 8; r++)
        if (((sel >> r) & 1) == 0) res &= m_rows[r];

    //Разряды 5 и 7 не подключены и читаются единицами, а разряд 6 - вход
    //магнитофона. Пока линия не подведена или на ней никого нет, он тоже
    //единица: так эта клавиатура и работала, пока ленты не было
    m_last = res | 0xA0 | (ear_level()?0x40:0);
    return m_last;
}

// Что на линии магнитофона. Неподведенная линия и линия, которую никто не
// ведет (_FFFF у ленты, лежащей в лентопротяжке молча), читаются единицей
bool ZXKeyboard::ear_level() const
{
    if (i_ear.linked == 0 || i_ear.value == _FFFF) return true;
    return (i_ear.value & 1) != 0;
}

unsigned int ZXKeyboard::get_direct(unsigned int address)
{
    return get_value(address);
}

void ZXKeyboard::set_value(MAYBE_UNUSED unsigned int address, MAYBE_UNUSED unsigned int value, MAYBE_UNUSED bool force)
{
    //Запись в этот порт у Спектрума - цвет бордюра и динамик. У Арго они
    //разведены отдельным портом ($FE на запись, устройство zx-port), чтобы
    //разряды можно было развести по своим линиям; здесь не делается ничего
}

std::vector<DeviceFieldInfo> ZXKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"rows", "All eight half-rows as the machine would read them", false});
    r.push_back({"ear",  "Tape input, bit 6 of the port: 1 when nothing drives it", false});
    r.push_back({"scan", "1 while key scanning is on, F10 toggles it",                 false});
    return r;
}

bool ZXKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "scan")
    {
        out.numeric = true; out.width = 8;
        out.values.push_back(m_scan?1:0);
        return true;
    }
    if (field == "ear")
    {
        out.numeric = true; out.width = 8;
        out.values.push_back(ear_level()?1:0);
        return true;
    }
    if (field == "rows")
    {
        out.numeric = true; out.width = 8;
        //То, что прочитает машина: при выключенном сканировании клавиш нет
        if (!m_scan)
        {
            for (unsigned int r = 0; r < 8; r++) out.values.push_back(0xFF);
            return true;
        }
        for (unsigned int r = 0; r < 8; r++) out.values.push_back(m_rows[r] | 0xE0u);
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void ZXKeyboard::reset(MAYBE_UNUSED bool cold)
{
    //После сброса клавиатуры нет, пока не нажмут Ф10
    m_scan = false;
    m_f10_down = false;
    m_ticks = 0;
    m_control = 0;
    for (unsigned int r = 0; r < 8; r++) m_rows[r] = 0x1F;
}

// Признак сканирования и команда лентопротяжке - защелки самой платы, а не
// состояние хоста: снимок, снятый в режиме ZX с включенной клавиатурой, должен
// открываться с включенной. Матрица сюда не идет - ее держат нажатые клавиши
// хоста, а их после восстановления никто не держит, и она соберется заново
// на первом же опросе
void ZXKeyboard::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.b("scan", m_scan);
    w.u("control", m_control, 8);
}

emulator::Result ZXKeyboard::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.b("scan", m_scan);
    r.u("control", m_control);
    m_f10_down = false;
    return emulator::Result::ok();
}

// Разряды команды те же, что понимает лентопротяжка: она их и получает
#define ZXK_PLAY     0x10
#define ZXK_BACK     0x20
#define ZXK_FORWARD  0x40
#define ZXK_STOP     0x80

void ZXKeyboard::clock(unsigned int counter)
{
    //Раз в миллисекунду: перепада нажатия этого хватает с запасом, а
    //ids_held() копирует вектор под замком и на каждую команду его звать
    //нельзя
    m_ticks += counter;
    const unsigned int step = (m_system_clock > 0)?(m_system_clock / 1000):1000;
    if (m_ticks < step) return;
    m_ticks -= step;
    sample_keys();
}

// Ф10 и стрелки железо разбирает само - процессор об этом не знает, потому
// управление лентой и работает под запущенной игрой
void ZXKeyboard::sample_keys()
{
    if (Source == nullptr) return;
    const std::vector<std::string> held = Source->ids_held();

    for (unsigned int r = 0; r < 8; r++)
    {
        unsigned int v = 0x1F;
        for (unsigned int b = 0; b < 5; b++)
        {
            const std::string & id = m_matrix[r][b];
            if (id.empty()) continue;
            for (size_t i = 0; i < held.size(); i++)
                if (held[i] == id) { v &= ~(1u << b); break; }
        }
        m_rows[r] = (uint8_t)v;
    }

    bool f10 = false, up = false, down = false, left = false, right = false;
    for (size_t i = 0; i < held.size(); i++)
    {
        const std::string & k = held[i];
        if      (k == "key_f10")   f10 = true;
        else if (k == "key_up")    up = true;
        else if (k == "key_down")  down = true;
        else if (k == "key_left")  left = true;
        else if (k == "key_right") right = true;
    }

    //Ф10 переключает сканирование по нажатию, а не по уровню
    if (f10 && !m_f10_down) m_scan = !m_scan;
    m_f10_down = f10;

    //Стрелки работают только при включённом сканировании: пока железо не
    //опрашивает клавиатуру, оно и стрелок не видит
    if (!m_scan) return;

    //Команда держится до следующей стрелки, как кнопка лентопротяжки: иначе
    //лента вставала бы на отпускании клавиши
    unsigned int c = m_control;
    if (up)         c = ZXK_PLAY;
    else if (down)  c = ZXK_STOP;
    else if (left)  c = ZXK_BACK;
    else if (right) c = ZXK_FORWARD;
    if (c != m_control)
    {
        m_control = c;
        i_control.change(c);
    }
}

ComputerDevice * create_zx_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new ZXKeyboard(im, cd);
}
