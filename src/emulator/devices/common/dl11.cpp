// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DEC DL11 serial line unit, going out through a host serial port

#include "dl11.h"
#include "emulator/utils.h"

#define REG_RCSR        0
#define REG_RBUF        1
#define REG_XCSR        2
#define REG_XBUF        3

#define CSR_DONE        0000200     // разряд 7 - готовность
#define CSR_IE          0000100     // разряд 6 - разрешение прерывания
#define XCSR_MAINT      0000004     // разряд 2 - петля
#define XCSR_BREAK      0000001     // разряд 0 - обрыв
#define RCSR_OVERRUN    0010000     // разряд 12 - переполнение

#define CALLBACK_CHAIN  1
#define CALLBACK_IAKO   2
#define CALLBACK_INIT   3
#define CALLBACK_RXD    4

// Посылок на символ: старт, восемь данных и стоп
#define BITS_PER_CHAR   10

DL11::DL11(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , m_irq(i_virq, i_vector)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
    , i_init(this, im, 1, "init", MODE_R, CALLBACK_INIT)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
    , i_rx_free(this, im, 1, "rx_free", MODE_W)
    , i_cts(this, im, 1, "cts", MODE_R)
{
    m_clocked = true;   // clock() переопределён
    can_read = true;
    can_write = true;
    addresable_size = 8;
}

emulator::Result DL11::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    m_rx_vector = read_confg_value(cd, "rx_vector", false, (unsigned int)060);
    m_tx_vector = read_confg_value(cd, "tx_vector", false, (unsigned int)064);
    m_rcsr_mask = read_confg_value(cd, "rcsr_mask", false, (unsigned int)CSR_IE) & 0177;
    m_xbuf_read = read_confg_value(cd, "xbuf_read", false, (unsigned int)0) & 0377;
    const std::string station = str_trim(read_confg_value(cd, "station", false, std::string("")));
    if (!station.empty()) set_station((int)parse_numeric_value(station, 10));
    m_baud = read_confg_value(cd, "baud", false, (unsigned int)9600);
    if (m_baud == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "Incorrect baud rate")) + "} " + name);
    m_char_ticks = (uint64_t)m_system_clock * BITS_PER_CHAR / m_baud;

    // Порт хоста, заданный конфигурацией. Если его нет или он занят, это не
    // мешает машине: линия просто никуда не выходит, поле connected - 0
    m_line.open_configured(read_confg_value(cd, "port", false, std::string("")), m_baud);

    return emulator::Result::ok();
}

// Сброс регистров - и при включении, и по INIT магистрали: приёмник пуст,
// передатчик свободен, прерывания, петля и обрыв сняты. Заводской тест СА
// проверяет это сразу после команды RESET
void DL11::init_registers()
{
    m_rcsr = 0;
    m_rbuf = 0;
    m_xcsr = CSR_DONE;
    m_xbuf = 0;
    m_tx_busy = false;
    m_tx_hold = false;
    m_tx_left = 0;
    m_rx_left = 0;
    m_rx_pending = false;
    m_tx_pending = false;
    update_irq();
    update_rx_free();
}

// Готовность приёмника наружу: 1, пока в нём нет непрочитанного байта
void DL11::update_rx_free()
{
    if (i_rx_free.linked == 0) return;
    const unsigned int v = (m_rcsr & CSR_DONE) ? 0 : 1;
    if ((i_rx_free.value & 1) != v) i_rx_free.change(v);
}

// Передатчик начинает символ, только когда на ~cts разрешение (если линия
// подключена)
bool DL11::tx_blocked()
{
    return i_cts.linked > 0 && (i_cts.value & 1) == 0;
}

void DL11::reset(MAYBE_UNUSED bool cold)
{
    m_line.clear_queue();
    init_registers();
}

//--------------------------- Линия -----------------------------------------//

void DL11::receive(uint8_t value)
{
    // Байт, пришедший в занятый приёмник, пропадает, а в RCSR встаёт
    // переполнение. Из порта хоста и из сценария так не бывает - оттуда байт
    // берётся, только когда приёмник свободен; переполниться могут петля и
    // линия к другому устройству (~rxd)
    if (m_rcsr & CSR_DONE) {
        m_rcsr |= RCSR_OVERRUN;
        m_overruns++;
        return;
    }
    m_rbuf = value;
    m_rcsr |= CSR_DONE;
    m_received++;
    if (m_rcsr & CSR_IE) m_rx_pending = true;
    update_irq();
    update_rx_free();
}

void DL11::transmit_done()
{
    const uint8_t b = (uint8_t)(m_xbuf & 0xFF);
    m_tx_busy = false;
    m_sent++;
    put(b);

    // Петля (разряд 2) и заглушка на разъёме возвращают байт в свой приёмник
    if ((m_xcsr & XCSR_MAINT) || m_plug)
        receive(b);
    else {
        m_line.write(b);
        if (i_txd.linked > 0) i_txd.change(b);
    }

    m_xcsr |= CSR_DONE;
    if (m_xcsr & CSR_IE) m_tx_pending = true;
    update_irq();
}

void DL11::clock(unsigned int counter)
{
    if (m_tx_busy) {
        if (m_tx_hold) {
            if (!tx_blocked()) m_tx_hold = false;
        } else {
            m_tx_left -= counter;
            if (m_tx_left <= 0) transmit_done();
        }
    }

    // Приёмник смотрит на линию раз в время символа: так байты идут с
    // настоящей скоростью, а порт хоста не опрашивается на каждой команде
    if (m_rx_left > 0) {
        m_rx_left -= counter;
        return;
    }
    if (m_rcsr & CSR_DONE) return;

    m_rx_left = (int64_t)m_char_ticks;
    // В петле и с заглушкой линия хоста отключена, чужое сюда не приходит
    uint8_t b;
    if (m_line.next(b, !((m_xcsr & XCSR_MAINT) || m_plug))) receive(b);
}

// Номер станции СА: разряды 0-3 номера ложатся в разряды 8-11 регистров,
// разряды 4-5 - в 13-14, мимо разряда 12 (переполнения). Так же его кладёт
// UKNCBTL и так же собирает обратно ПЗУ (165304)
void DL11::set_station(int station)
{
    if (station < 0) { m_station = -1; m_station_bits = 0; return; }
    m_station = station & 077;
    const unsigned int n = (unsigned int)m_station;
    m_station_bits = ((n & 017) | ((n & 060) << 1)) << 8;
}

//--------------------------- Прерывания ------------------------------------//

void DL11::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
    if (callback_id == CALLBACK_RXD) {
        // Байт с другого конца линии. Устройство там выставляет его, когда
        // последняя посылка уже прошла (так делают и dl11, и i8251), и
        // приёмник берёт его сразу: не успела программа забрать прошлый -
        // переполнение, как на настоящей линии. Очередь - только для
        // сценария и порта хоста, которые ждут готовности приёмника
        receive((uint8_t)(new_value & 0xFF));
        return;
    }
    if (callback_id == CALLBACK_INIT) {
        // Импульс RESET - это change(1) и change(0), и сброс делает первая
        // половина. Прежнее значение не проверяется: до первого RESET линия
        // хранит _FFFF, и по нему первый импульс был бы пропущен
        if (new_value & 1) init_registers();
        return;
    }
    if (callback_id == CALLBACK_IAKO) {
        // Процессор взял прерывание с этим вектором - снять его запрос
        const unsigned int vector = new_value & 0xFFFF;
        if (vector == 0) return;
        if (vector == m_rx_vector) m_rx_pending = false;
        if (vector == m_tx_vector) m_tx_pending = false;
    }
    // Иначе - чужой запрос по цепочке: пересчитать предложенное процессору
    update_irq();
}

void DL11::update_irq()
{
    // Свой запрос вперёд чужого, приём старше передачи
    const unsigned int own = m_rx_pending ? m_rx_vector : (m_tx_pending ? m_tx_vector : 0);
    m_irq.offer(VirqLine::chain(own, i_virq_in, i_vector_in));
}

//--------------------------- Регистры --------------------------------------//

void DL11::set_rcsr(unsigned int value)
{
    const bool was = (m_rcsr & CSR_IE) != 0;
    m_rcsr = (m_rcsr & ~m_rcsr_mask) | (value & m_rcsr_mask);
    const bool now = (m_rcsr & CSR_IE) != 0;
    if (!now) m_rx_pending = false;
    else if (!was && (m_rcsr & CSR_DONE)) m_rx_pending = true;
    update_irq();
}

void DL11::set_xcsr(unsigned int value)
{
    const unsigned int writable = CSR_IE | XCSR_MAINT | XCSR_BREAK;
    const bool was = (m_xcsr & CSR_IE) != 0;
    m_xcsr = (m_xcsr & ~writable) | (value & writable);
    const bool now = (m_xcsr & CSR_IE) != 0;
    if (!now) m_tx_pending = false;
    else if (!was && (m_xcsr & CSR_DONE)) m_tx_pending = true;
    update_irq();
}

unsigned int DL11::get_value_word(unsigned int address)
{
    // Номер станции СА виден в старших разрядах любого регистра
    switch ((address >> 1) & 3) {
    case REG_RCSR: return m_rcsr | m_station_bits;
    case REG_RBUF: {
        const unsigned int v = m_rbuf & 0xFF;
        m_rcsr &= ~(CSR_DONE | RCSR_OVERRUN);
        m_rx_pending = false;
        update_irq();
        update_rx_free();
        return v | m_station_bits;
    }
    case REG_XCSR: return m_xcsr | m_station_bits;
    // XBUF только для записи; чтение даёт постоянный байт, у УК-НЦ - 0360 у СА
    // и 0370 у С2 (их ждёт заводской тест СА, так же отвечает UKNCBTL)
    default:       return m_xbuf_read | m_station_bits;
    }
}

void DL11::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    switch ((address >> 1) & 3) {
    case REG_RCSR: set_rcsr(value); break;
    case REG_XCSR: set_xcsr(value); break;
    case REG_XBUF:
        m_xbuf = value & 0xFF;
        m_xcsr &= ~CSR_DONE;
        m_tx_pending = false;
        m_tx_busy = true;
        m_tx_hold = tx_blocked();
        m_tx_left = (int64_t)m_char_ticks;
        update_irq();
        break;
    default:
        break;                      // RBUF не пишется
    }
}

// Отладчик и LOG видят принятый байт, не снимая готовности приёмника
unsigned DL11::get_direct(unsigned address)
{
    const unsigned int w = (((address >> 1) & 3) == REG_RBUF)
                           ? ((m_rbuf & 0xFF) | m_station_bits)
                           : get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

unsigned int DL11::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void DL11::set_value(unsigned int address, unsigned int value, bool force)
{
    // Все разряды, которые пишет программа, - в младшем байте, и передачу
    // начинает запись младшего байта XBUF. Запись старшего байта ничего не
    // меняет: раньше она становилась словом с нулевым младшим байтом, и MOVB
    // в 176577 передавал ноль, а в 176571 снимал разрешение прерывания
    if (address & 1) return;
    set_value_word(address, value & 0xFF, force);
}

//--------------------------- Сценарии --------------------------------------//

ConfigFields DL11::get_config_fields()
{
    //The host port the line goes to; empty leaves the line unconnected. Most
    //lines are inside the machine (to the terminal, the keyboard), so the
    //choice is offered only where the config says the line goes out of it
    ConfigFields r;
    const std::string host_port = str_trim(cd->get_parameter("host_port", false).value);
    if (!host_port.empty() && parse_numeric_value(host_port, 10) != 0) {
        ConfigField f;
        f.name = "port";
        f.title = QT_TRANSLATE_NOOP("ConfigFields", "Serial port of the computer");
        f.type = CONFIG_FIELD_STRING;
        r.push_back(f);
    }

    //The station number means something only to a network adapter, and the
    //config says which line is one by giving it a number at all
    if (!str_trim(cd->get_parameter("station", false).value).empty()) {
        ConfigField s;
        s.name = "station";
        s.title = QT_TRANSLATE_NOOP("ConfigFields", "Network station number (0-63)");
        s.type = CONFIG_FIELD_STRING;
        s.def = "0";
        r.push_back(s);
    }
    return r;
}

void DL11::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("rcsr", m_rcsr);
    w.u("rbuf", m_rbuf);
    w.u("xcsr", m_xcsr);
    w.u("xbuf", m_xbuf);
    w.b("rx_pending", m_rx_pending);
    w.b("tx_pending", m_tx_pending);
    w.n64("char_ticks", m_char_ticks);
    w.b("tx_busy", m_tx_busy);
    w.b("tx_hold", m_tx_hold);
    w.n64("tx_left", (uint64_t)m_tx_left);
    w.n("sent", m_sent);
    w.n("received", m_received);
    //The loopback plug: part of what the program on the machine sees - with
    //it on, what it sends comes back - so a snapshot taken with it plugged
    //has to open with it plugged
    w.b("plug", m_plug);
    //What is offered on the line, which the restored line itself does not
    //say: without it the acknowledge would never withdraw a pending request
    w.u("offered", m_irq.offered());
    //What a script has pushed in and the line has not handed over yet
    m_line.save_queue(w);
}

emulator::Result DL11::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("rcsr", m_rcsr);
    r.u("rbuf", m_rbuf);
    r.u("xcsr", m_xcsr);
    r.u("xbuf", m_xbuf);
    r.b("rx_pending", m_rx_pending);
    r.b("tx_pending", m_tx_pending);
    r.n64("char_ticks", m_char_ticks);
    r.b("tx_busy", m_tx_busy);
    r.b("tx_hold", m_tx_hold);
    uint64_t tx_left = 0;
    if (r.n64("tx_left", tx_left)) m_tx_left = (int64_t)tx_left;
    r.u("sent", m_sent);
    r.u("received", m_received);
    r.b("plug", m_plug);
    unsigned offered = 0;
    r.u("offered", offered);
    m_irq.set_offered(offered);
    m_line.load_queue(r);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> DL11::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"rcsr",      "Состояние приёмника",                                   false});
    r.push_back({"xcsr",      "Состояние передатчика",                                 false});
    r.push_back({"connected", "1, когда линия выходит на порт хоста",                  false});
    r.push_back({"port",      "Имя порта хоста",                                       false});
    r.push_back({"sent",      "Сколько байтов передано",                               false});
    r.push_back({"received",  "Сколько байтов принято",                                false});
    r.push_back({"overruns",  "Сколько байтов пропало: приёмник был занят",            false});
    r.push_back({"queued",    "Сколько байтов сценария ждут приёма",                   false});
    r.push_back({"vector",    "Вектор, предложенный процессору, или 0",                false});
    r.push_back({"station",   "Номер станции СА, -1 - его нет",                        false});
    r.push_back({"plug",      "1, когда на разъёме заглушка: выход замкнут на вход",   false});
    add_output_fields(r);
    return r;
}

bool DL11::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "port") {
        out.text = m_line.port_name();
        return true;
    }
    if (get_output_field(field, from, to, out)) return true;

    out.numeric = true;
    out.width = 16;
    if (field == "rcsr")      { out.values.push_back(m_rcsr);                    return true; }
    if (field == "xcsr")      { out.values.push_back(m_xcsr);                    return true; }
    if (field == "vector")    { out.values.push_back(m_irq.offered());                 return true; }
    if (field == "station")   { out.values.push_back((unsigned int)m_station);   return true; }
    if (field == "plug")      { out.values.push_back(m_plug? 1 : 0);             return true; }
    if (field == "connected") { out.values.push_back(m_line.connected()? 1 : 0); return true; }
    out.width = 32;
    if (field == "sent")      { out.values.push_back(m_sent);                    return true; }
    if (field == "received")  { out.values.push_back(m_received);                return true; }
    if (field == "overruns")  { out.values.push_back(m_overruns); return true; }
    if (field == "queued")    { out.values.push_back((unsigned int)m_line.queued()); return true; }

    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> DL11::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"connect",    "\"port\"", "Выводит линию на порт хоста: COM5, /dev/ttyUSB0"});
    r.push_back({"disconnect", "",         "Отключает линию от порта хоста"});
    r.push_back({"send",       "\"text\"", "Подаёт байты в приёмник (\\n, \\r, \\t, \\ooo)"});
    r.push_back({"sendfile",   "\"file\"", "Подаёт в приёмник содержимое файла"});
    r.push_back({"station",    "n",        "Номер станции СА, 0-63"});
    r.push_back({"plug",       "[0|1]",    "Заглушка на разъёме: выход линии замкнут на вход"});
    return r;
}

emulator::Result DL11::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    const std::string arg = p.empty()? std::string() : p[0];

    // connect, disconnect, send, sendfile
    emulator::Result res;
    if (m_line.command(command, arg, m_baud, sd, res)) return res;

    if (command == "plug") {
        m_plug = arg.empty()? !m_plug : (parse_numeric_value(arg, 10) != 0);
        return emulator::Result::ok();
    }

    if (command == "station") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "Command 'station' expects a number")) + "}");
        set_station((int)parse_numeric_value(arg, 10));
        return emulator::Result::ok();
    }

    return AddressableDevice::send_command(command, parameters);
}

ComputerDevice * create_dl11(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DL11(im, cd);
}
