// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DEC DL11 serial line unit, going out through a host serial port

#include "dl11.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"

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

// Посылок на символ: старт, восемь данных и стоп
#define BITS_PER_CHAR   10

DL11::DL11(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_virq(this, im, 1, "virq", MODE_W)
    , i_vector(this, im, 16, "vector", MODE_W)
    , i_virq_in(this, im, 1, "virq_in", MODE_R, CALLBACK_CHAIN)
    , i_vector_in(this, im, 16, "vector_in", MODE_R)
    , i_iako(this, im, 16, "iako", MODE_R, CALLBACK_IAKO)
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
    m_baud = read_confg_value(cd, "baud", false, (unsigned int)9600);
    if (m_baud == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "Incorrect baud rate")) + "} " + name);
    m_char_ticks = (uint64_t)m_system_clock * BITS_PER_CHAR / m_baud;

    // Порт хоста, заданный конфигурацией. Если его нет или он занят, это не
    // мешает машине: линия просто никуда не выходит, поле connected - 0
    m_port_name = str_trim(read_confg_value(cd, "port", false, std::string("")));
    if (!m_port_name.empty()) (void)m_host.open(m_port_name, m_baud);

    return emulator::Result::ok();
}

void DL11::reset(MAYBE_UNUSED bool cold)
{
    m_rcsr = 0;
    m_rbuf = 0;
    m_xcsr = CSR_DONE;
    m_xbuf = 0;
    m_tx_busy = false;
    m_tx_left = 0;
    m_rx_left = 0;
    m_rx_pending = false;
    m_tx_pending = false;
    m_rx_queue.clear();
    update_irq();
}

//--------------------------- Линия -----------------------------------------//

void DL11::receive(uint8_t value)
{
    // Байт, пришедший в занятый приёмник, пропадает, а в RCSR встаёт
    // переполнение. Из порта хоста и из сценария так не бывает - оттуда байт
    // берётся, только когда приёмник свободен; переполниться может петля
    if (m_rcsr & CSR_DONE) {
        m_rcsr |= RCSR_OVERRUN;
        return;
    }
    m_rbuf = value;
    m_rcsr |= CSR_DONE;
    m_received++;
    if (m_rcsr & CSR_IE) m_rx_pending = true;
    update_irq();
}

void DL11::transmit_done()
{
    const uint8_t b = (uint8_t)(m_xbuf & 0xFF);
    m_tx_busy = false;
    m_output[m_sent++ & (OUTPUT_SIZE - 1)] = b;

    if (m_xcsr & XCSR_MAINT)
        receive(b);
    else
        (void)m_host.write(b);

    m_xcsr |= CSR_DONE;
    if (m_xcsr & CSR_IE) m_tx_pending = true;
    update_irq();
}

void DL11::clock(unsigned int counter)
{
    if (m_tx_busy) {
        m_tx_left -= counter;
        if (m_tx_left <= 0) transmit_done();
    }

    // Приёмник смотрит на линию раз в время символа: так байты идут с
    // настоящей скоростью, а порт хоста не опрашивается на каждой команде
    if (m_rx_left > 0) {
        m_rx_left -= counter;
        return;
    }
    if (m_rcsr & CSR_DONE) return;

    m_rx_left = (int64_t)m_char_ticks;
    if (!m_rx_queue.empty()) {
        const uint8_t b = m_rx_queue.front();
        m_rx_queue.pop_front();
        receive(b);
        return;
    }
    // В петле передатчик отключён от линии, и чужое сюда не приходит
    if (m_xcsr & XCSR_MAINT) return;
    uint8_t b;
    if (m_host.read(b)) receive(b);
}

//--------------------------- Прерывания ------------------------------------//

void DL11::interface_callback(unsigned int callback_id, unsigned int new_value, MAYBE_UNUSED unsigned int old_value)
{
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
    unsigned int vector = 0;

    // Свой запрос вперёд чужого, приём старше передачи
    if (m_rx_pending)                        vector = m_rx_vector;
    else if (m_tx_pending)                   vector = m_tx_vector;
    else if ((i_virq_in.value & 1) == 0)     vector = i_vector_in.value & 0xFFFF;

    // Процессор слышит запрос по фронту: при смене источника линия
    // отпускается и прижимается заново (как у uknc-timer)
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

//--------------------------- Регистры --------------------------------------//

void DL11::set_rcsr(unsigned int value)
{
    const bool was = (m_rcsr & CSR_IE) != 0;
    m_rcsr = (m_rcsr & ~CSR_IE) | (value & CSR_IE);
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
    switch ((address >> 1) & 3) {
    case REG_RCSR: return m_rcsr;
    case REG_RBUF: {
        const unsigned int v = m_rbuf & 0xFF;
        m_rcsr &= ~(CSR_DONE | RCSR_OVERRUN);
        m_rx_pending = false;
        update_irq();
        return v;
    }
    case REG_XCSR: return m_xcsr;
    default:       return 0;        // XBUF только для записи
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
        m_tx_left = (int64_t)m_char_ticks;
        update_irq();
        break;
    default:
        break;                      // RBUF не пишется
    }
}

unsigned int DL11::get_value(unsigned int address)
{
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void DL11::set_value(unsigned int address, unsigned int value, bool force)
{
    // Байт ложится на свою половину слова; значащие разряды всех регистров
    // в младшем байте, кроме переполнения, которое программа не пишет
    const unsigned int b = value & 0xFF;
    set_value_word(address & ~1u, (address & 1)? (b << 8) : b, force);
}

//--------------------------- Сценарии --------------------------------------//

// Строка команды send: \n, \r, \t, \\, \" и восьмеричные \ooo
static std::string decode_send_text(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] != '\\' || i + 1 >= s.length()) { r += s[i]; continue; }
        const char c = s[++i];
        if (c == 'n')      r += '\n';
        else if (c == 'r') r += '\r';
        else if (c == 't') r += '\t';
        else if (c >= '0' && c <= '7') {
            unsigned int v = (unsigned int)(c - '0');
            for (int k = 0; k < 2 && i + 1 < s.length() && s[i+1] >= '0' && s[i+1] <= '7'; k++)
                v = v * 8 + (unsigned int)(s[++i] - '0');
            r += (char)(v & 0xFF);
        } else r += c;
    }
    return r;
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
    r.push_back({"queued",    "Сколько байтов сценария ждут приёма",                   false});
    r.push_back({"vector",    "Вектор, предложенный процессору, или 0",                false});
    r.push_back({"output",    "Последние переданные байты, старые первыми; output(n) - последние n", true});
    return r;
}

bool DL11::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "port") {
        out.text = m_port_name;
        return true;
    }
    if (field == "output") {
        unsigned int n = (from == 0 && to == 0)? OUTPUT_SIZE : ((from > OUTPUT_SIZE)? OUTPUT_SIZE : from);
        if (n > m_sent) n = m_sent;
        out.numeric = true;
        out.width = 8;
        for (unsigned int i = n; i > 0; i--)
            out.values.push_back(m_output[(m_sent - i) & (OUTPUT_SIZE - 1)]);
        return true;
    }

    out.numeric = true;
    out.width = 16;
    if (field == "rcsr")      { out.values.push_back(m_rcsr);                    return true; }
    if (field == "xcsr")      { out.values.push_back(m_xcsr);                    return true; }
    if (field == "vector")    { out.values.push_back(m_offered);                 return true; }
    if (field == "connected") { out.values.push_back(m_host.is_open()? 1 : 0);   return true; }
    out.width = 32;
    if (field == "sent")      { out.values.push_back(m_sent);                    return true; }
    if (field == "received")  { out.values.push_back(m_received);                return true; }
    if (field == "queued")    { out.values.push_back((unsigned int)m_rx_queue.size()); return true; }

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
    return r;
}

emulator::Result DL11::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    const std::string arg = p.empty()? std::string() : p[0];

    if (command == "connect") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "Command 'connect' expects a port name")) + "}");
        m_port_name = arg;
        return m_host.open(arg, m_baud);
    }

    if (command == "disconnect") {
        m_host.close();
        return emulator::Result::ok();
    }

    if (command == "send") {
        for (char c : decode_send_text(arg)) m_rx_queue.push_back((uint8_t)c);
        return emulator::Result::ok();
    }

    if (command == "sendfile") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "Command 'sendfile' expects a file name")) + "}");
        std::string file = find_file_location(sd, arg);
        if (file.empty()) file = arg;
        dsk_tools::UTF8_ifstream f(file, std::ios::binary);
        if (!f.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{DL11|" + std::string(QT_TRANSLATE_NOOP("DL11", "File not found")) + "} " + arg);
        // UTF8_ifstream у MinGW - не поток, а обёртка над файлом Windows с
        // одним read(), поэтому файл читается кусками
        char buf[512];
        std::streamsize n;
        while ((n = f.read(buf, sizeof(buf))) > 0)
            for (std::streamsize i = 0; i < n; i++) m_rx_queue.push_back((uint8_t)buf[i]);
        f.close();
        return emulator::Result::ok();
    }

    return AddressableDevice::send_command(command, parameters);
}

ComputerDevice * create_dl11(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new DL11(im, cd);
}
