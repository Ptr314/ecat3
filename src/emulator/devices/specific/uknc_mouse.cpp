// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ mouse, register 177400 of the peripheral processor

#include "uknc_mouse.h"
#include "emulator/utils.h"

// Больше стольких шагов в запасе не копится: рывок хоста не должен потом
// долго дотягиваться
#define UKNC_MOUSE_MAX_PENDING  256
// Смещение за одно чтение
#define UKNC_MOUSE_MAX_DELTA    63

UKNCMouse::UKNCMouse(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , m_on(true)
    , m_used(false)
    , m_dx(0)
    , m_dy(0)
    , m_buttons(0)
{
    device_class = "mouse";
    addresable_size = 2;
    can_read = true;
}

emulator::Result UKNCMouse::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    const std::string mapper = str_trim(cd->get_parameter("mapper", false).value);
    m_mapper = dynamic_cast<MemoryMapper*>(im->dm->get_device_by_name(mapper.empty() ? "mapper" : mapper, false));
    if (m_mapper == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Incorrect parameters for")) + "} " + name);
    m_on = read_confg_value(cd, "plugged", false, true);
    m_icon = str_trim(cd->get_parameter("icon", false).value);
    return emulator::Result::ok();
}

// Любой сброс: программа, которая читала мышь, кончилась, и щелчок снова
// просто щелчок, пока новая её не прочтёт
void UKNCMouse::reset(bool cold)
{
    AddressableDevice::reset(cold);
    m_used = false;
}

void UKNCMouse::add_pending(std::atomic<int> &axis, int delta)
{
    int current = axis.load();
    int value;
    do {
        value = current + delta;
        if (value > UKNC_MOUSE_MAX_PENDING) value = UKNC_MOUSE_MAX_PENDING;
        if (value < -UKNC_MOUSE_MAX_PENDING) value = -UKNC_MOUSE_MAX_PENDING;
    } while (!axis.compare_exchange_weak(current, value));
}

static int clamp_delta(int v)
{
    if (v > UKNC_MOUSE_MAX_DELTA) return UKNC_MOUSE_MAX_DELTA;
    if (v < -UKNC_MOUSE_MAX_DELTA) return -UKNC_MOUSE_MAX_DELTA;
    return v;
}

// Забирает из запаса не больше, чем помещается в регистр; остаток ждёт
// следующего чтения
int UKNCMouse::take(std::atomic<int> &axis)
{
    int current = axis.load();
    int t;
    do {
        t = clamp_delta(current);
    } while (!axis.compare_exchange_weak(current, current - t));
    return t;
}

// dx вправо, dy вниз - как у хоста; в регистре Y растёт вверх
unsigned UKNCMouse::compose(int dx, int dy, unsigned buttons)
{
    const unsigned x = static_cast<unsigned>(dx) & 0177;
    const unsigned y = static_cast<unsigned>(-dy) & 0177;
    return (x << 1) | ((buttons & 2) ? 1 : 0)
         | (y << 9) | ((buttons & 1) ? 0400 : 0);
}

unsigned UKNCMouse::read_word()
{
    const int dx = take(m_dx);
    const int dy = take(m_dy);
    m_last = compose(dx, dy, m_buttons.load());
    m_reads++;
    m_used = true;
    return m_last;
}

// Любое чтение - байта или слова - забирает смещение целиком: процессор
// читает регистр одним циклом шины
unsigned UKNCMouse::get_value(unsigned address)
{
    const unsigned w = read_word();
    return (address & 1) ? (w >> 8) : (w & 0377);
}

unsigned int UKNCMouse::get_value_word(unsigned int address)
{
    (void)address;
    return read_word();
}

// Отладчику и LOG value - то, что прочиталось бы, без изъятия из запаса
unsigned UKNCMouse::get_direct(unsigned address)
{
    const unsigned w = compose(clamp_delta(m_dx.load()), clamp_delta(m_dy.load()), m_buttons.load());
    return (address & 1) ? (w >> 8) : (w & 0377);
}

void UKNCMouse::set_value(unsigned int address, unsigned int value, bool force)
{
    (void)address; (void)value; (void)force;
}

unsigned int UKNCMouse::route(unsigned int address, unsigned int mode)
{
    (void)address; (void)mode;
    return m_on ? ROUTE_ANSWER : ROUTE_PASS;
}

DeviceOptions UKNCMouse::get_device_options()
{
    return { mouse_switch_option(0, m_icon, m_on) };
}

// Поток эмуляции. Выключенная мышь ничего не держит; диспетчер забывает
// разобранные страницы, раз ответ route() стал другим
void UKNCMouse::set_device_option(unsigned option_id, unsigned value_id)
{
    if (option_id != 0) return;
    m_on = (value_id != 0);
    m_dx = 0;
    m_dy = 0;
    m_buttons = 0;
    if (m_mapper != nullptr) m_mapper->routing_changed();
}

void UKNCMouse::move(int dx, int dy, int buttons)
{
    if (!m_on) return;
    if (buttons >= 0) m_buttons = static_cast<unsigned>(buttons) & 3;
    if (dx != 0) add_pending(m_dx, dx);
    if (dy != 0) add_pending(m_dy, dy);
}

std::vector<DeviceFieldInfo> UKNCMouse::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"x",       "Шаги вправо, ещё не прочитанные (влево - отрицательные)", false});
    r.push_back({"y",       "Шаги вниз, ещё не прочитанные (вверх - отрицательные)", false});
    r.push_back({"buttons", "Кнопки: разряд 0 - левая, 1 - правая", false});
    r.push_back({"pending", "Шаги, ещё не прочитанные, по обеим осям", false});
    r.push_back({"plugged", "1 - мышь включена кнопкой на панели", false});
    r.push_back({"used",    "1 - программа читала регистр после сброса (щелчок захватит указатель)", false});
    r.push_back({"last",    "Последнее прочитанное слово 177400", false});
    r.push_back({"reads",   "Сколько раз регистр прочитан", false});
    return r;
}

bool UKNCMouse::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "x")       { out.values.push_back(static_cast<unsigned>(m_dx.load())); return true; }
    if (field == "y")       { out.values.push_back(static_cast<unsigned>(m_dy.load())); return true; }
    if (field == "buttons") { out.values.push_back(m_buttons.load()); return true; }
    if (field == "pending") {
        const int x = m_dx.load(), y = m_dy.load();
        out.values.push_back(static_cast<unsigned>((x < 0 ? -x : x) + (y < 0 ? -y : y)));
        return true;
    }
    if (field == "plugged") { out.values.push_back(m_on ? 1 : 0); return true; }
    if (field == "used")    { out.values.push_back(m_used ? 1 : 0); return true; }
    out.width = 16;
    if (field == "last")    { out.values.push_back(m_last); return true; }
    out.width = 0;
    if (field == "reads")   { out.values.push_back(m_reads); return true; }
    out.numeric = false;
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_mouse(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCMouse(im, cd);
}
