// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Slot cards of the Правец-8А that answer their own ROM pages

#include "apple2_cards.h"
#include "emulator/utils.h"
#include "dsk_tools/core.h"
#include "libs/dsk_tools/src/utils.h"

// ---------------------------- Parallel printer card -------------------------

Apple2ParallelCard::Apple2ParallelCard(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_data(this, im, 8, "data", MODE_W)
    , i_strobe(this, im, 1, "strobe", MODE_W)
    , i_busy(this, im, 1, "busy", MODE_R)
{
}

emulator::Result Apple2ParallelCard::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_rom = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("rom").value));
    if (m_rom == nullptr || m_rom->get_size() < 0x800)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "A ROM of 2K is expected")) + "} " + name);
    m_invert = read_confg_value(cd, "invert", false, false);
    return emulator::Result::ok();

}

unsigned Apple2ParallelCard::get_direct(unsigned address)
{
    if (address < 0xC100) return 0xFF;
    const unsigned offset = (address < 0xC800) ? (address & 0xFF) : (address & 0x7FF);
    unsigned v = m_rom->get_buffer()[offset];
    if (address < 0xC800 && (address & 0xC0) == 0xC0)
        v = (v & 0x7F) | ((i_busy.value & 1) ? 0x80 : 0);
    return v;
}

unsigned Apple2ParallelCard::get_value(unsigned address)
{
    return get_direct(address);
}

void Apple2ParallelCard::set_value(unsigned address, unsigned value, bool force)
{
    (void)force;
    if (address >= 0xC100) return;
    // The latch takes the byte, the strobe pulse follows. It starts from 0
    // every time: a line nobody has driven yet holds all ones, and the
    // printer would not see the first rise
    i_data.change((m_invert ? ~value : value) & 0xFF);
    i_strobe.change(0);
    i_strobe.change(1);
    i_strobe.change(0);
    m_written++;
}

std::vector<DeviceFieldInfo> Apple2ParallelCard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"written", "Bytes put out to the printer", false});
    r.push_back({"busy",    "The BUSY line of the printer", false});
    return r;
}

bool Apple2ParallelCard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "written" || field == "busy") {
        out.numeric = true;
        out.values.push_back(field == "written" ? m_written : (i_busy.value & 1));
        return true;
    }
    return AddressableDevice::get_field(field, from, to, out);
}

void Apple2ParallelCard::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n("written", m_written);
}

emulator::Result Apple2ParallelCard::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("written", m_written);
    return emulator::Result::ok();
}

// ---------------------------------- U-LAN -----------------------------------

#define ACIA_RDRF   0x01
#define ACIA_TDRE   0x02
#define ACIA_DCD    0x04
#define ACIA_OVRN   0x20
#define ACIA_IRQ    0x80

// Start, eight data bits and stop
#define ULAN_BITS_PER_CHAR  10

PravetzULan::PravetzULan(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
{
    m_clocked = true;
}

emulator::Result PravetzULan::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;
    m_rom = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("rom").value));
    m_rom_offset = read_confg_value(cd, "rom_offset", false, (unsigned)0);
    if (m_rom == nullptr || m_rom_offset + 0x1000 > m_rom->get_size())
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "A ROM of 4K is expected")) + "} " + name);
    m_station = read_confg_value(cd, "station", false, (unsigned)1) & 0x3F;
    m_dcd = read_confg_value(cd, "dcd", false, false);
    m_baud = read_confg_value(cd, "baud", false, (unsigned)115200);
    if (m_baud == 0 || m_system_clock == 0)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "Incorrect baud rate")) + "} " + name);
    update_rate();
    m_port_name = str_trim(read_confg_value(cd, "port", false, std::string("")));
    if (!m_port_name.empty()) (void)m_host.open(m_port_name, m_baud);
    PravetzULan::reset(true);
    return emulator::Result::ok();
}

void PravetzULan::reset(bool cold)
{
    (void)cold;
    // The ACIA has no reset pin: only its master reset clears it. A cold
    // start finds RTS low, the first bank
    if (cold) {
        m_control = 0;
        m_status = ACIA_TDRE;
        m_rx_queue.clear();
        m_echo.clear();
        m_rx_left = m_tx_left = 0;
    }
}

// The rate the counter divide select (control bits 0-1) makes of the line
// clock: ÷16 is the rate of the network
void PravetzULan::update_rate()
{
    const unsigned div = ((m_control & 3) == 2) ? 4 : 1;     // ÷64 is four times slower
    m_char_ticks = (uint64_t)m_system_clock * ULAN_BITS_PER_CHAR * div / m_baud;
    if (m_char_ticks == 0) m_char_ticks = 1;
}

unsigned PravetzULan::bank_base() const
{
    return m_rom_offset + ((((m_control >> 5) & 3) == 2) ? 0x800 : 0);
}

unsigned PravetzULan::rom_byte(unsigned offset) const
{
    return m_rom->get_buffer()[bank_base() + (offset & 0x7FF)];
}

void PravetzULan::write_control(uint8_t value)
{
    m_control = value;
    if ((value & 3) == 3) {
        // Master reset: the receiver and the transmitter start empty
        m_status = ACIA_TDRE;
        m_rx_queue.clear();
        m_echo.clear();
        m_tx_left = 0;
    }
    update_rate();
}

void PravetzULan::clock(unsigned int counter)
{
    // Own bytes come back from the line after their time on it
    if (m_tx_left > 0) {
        m_tx_left -= counter;
        if (m_tx_left <= 0) {
            m_status |= ACIA_TDRE;
            if (!m_echo.empty()) {
                m_rx_queue.push_back(m_echo.front());
                m_echo.pop_front();
            }
            if (!m_echo.empty()) m_tx_left = (int64_t)m_char_ticks;
        }
    }
    if (m_rx_left > 0) {
        m_rx_left -= counter;
        return;
    }
    if (m_rx_queue.empty() && m_host.is_open()) {
        uint8_t b;
        if (m_host.read(b)) m_rx_queue.push_back(b);
    }
    if (m_rx_queue.empty()) {
        m_rx_left = (int64_t)m_char_ticks;
        return;
    }
    if (m_status & ACIA_RDRF) m_status |= ACIA_OVRN;
    m_rdr = m_rx_queue.front();
    m_rx_queue.pop_front();
    m_status |= ACIA_RDRF;
    m_received++;
    m_rx_left = (int64_t)m_char_ticks;
}

unsigned PravetzULan::get_direct(unsigned address)
{
    if (address >= 0xC100) {
        // The station number on the data bus at $CFFF
        if (address == 0xCFFF) return m_station;
        return rom_byte(address < 0xC800 ? (address & 0xFF) : (address & 0x7FF));
    }
    if (address & 4) return m_rdr;
    return (m_status & ~ACIA_DCD) | (m_dcd ? ACIA_DCD : 0);
}

unsigned PravetzULan::get_value(unsigned address)
{
    const unsigned v = get_direct(address);
    if (address < 0xC100 && (address & 4)) m_status &= ~(ACIA_RDRF | ACIA_OVRN);
    return v;
}

void PravetzULan::set_value(unsigned address, unsigned value, bool force)
{
    (void)force;
    if (address >= 0xC100) return;
    value &= 0xFF;
    if ((address & 4) == 0) {
        write_control((uint8_t)value);
        return;
    }
    // A byte onto the line: to the other stations and back to this one
    m_status &= ~ACIA_TDRE;
    put((uint8_t)value);
    (void)m_host.write((uint8_t)value);
    m_echo.push_back((uint8_t)value);
    if (m_tx_left <= 0) m_tx_left = (int64_t)m_char_ticks;
    m_sent++;
}

std::vector<DeviceFieldInfo> PravetzULan::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"control",   "ACIA control register",                    false});
    r.push_back({"status",    "ACIA status register",                     false});
    r.push_back({"bank",      "ROM bank shown, 0 or 1 (RTS)",             false});
    r.push_back({"station",   "Station number the card gives at $CFFF",   false});
    r.push_back({"connected", "1 when the line goes out to a host port",  false});
    r.push_back({"sent",      "Bytes sent onto the line",                 false});
    r.push_back({"received",  "Bytes received, own ones included",       false});
    add_output_fields(r);
    return r;
}

bool PravetzULan::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (get_output_field(field, from, to, out)) return true;
    out.numeric = true;
    if (field == "control")   { out.values.push_back(m_control); return true; }
    if (field == "status")    { out.values.push_back(get_direct(0)); return true; }
    if (field == "bank")      { out.values.push_back(bank_base() != m_rom_offset ? 1 : 0); return true; }
    if (field == "station")   { out.values.push_back(m_station); return true; }
    if (field == "connected") { out.values.push_back(m_host.is_open() ? 1 : 0); return true; }
    out.width = 32;
    if (field == "sent")      { out.values.push_back(m_sent); return true; }
    if (field == "received")  { out.values.push_back(m_received); return true; }
    out.numeric = false;
    out.width = 0;
    return AddressableDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> PravetzULan::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = AddressableDevice::get_device_commands();
    r.push_back({"connect",    "\"port\"", "Takes the line out to a host port: COM5, /dev/ttyUSB0"});
    r.push_back({"disconnect", "",         "Disconnects the line from the host port"});
    r.push_back({"send",       "\"text\"", "Puts bytes of another station on the line (\\n, \\r, \\t, \\ooo)"});
    return r;
}

emulator::Result PravetzULan::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    const std::string arg = p.empty() ? std::string() : p[0];
    if (command == "connect") {
        if (arg.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "Command 'connect' expects a port name")) + "}");
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
    return AddressableDevice::send_command(command, parameters);
}

void PravetzULan::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("control", m_control, 8);
    w.u("status", m_status, 8);
    w.u("rdr", m_rdr, 8);
    w.n64("rx_left", (uint64_t)m_rx_left);
    w.n64("tx_left", (uint64_t)m_tx_left);
    w.n("sent", m_sent);
    w.n("received", m_received);
    std::vector<uint8_t> rx(m_rx_queue.begin(), m_rx_queue.end());
    w.n64("rx_queued", rx.size());
    if (!rx.empty()) w.array("rx_queue", rx.data(), rx.size());
    std::vector<uint8_t> echo(m_echo.begin(), m_echo.end());
    w.n64("echo_queued", echo.size());
    if (!echo.empty()) w.array("echo", echo.data(), echo.size());
}

emulator::Result PravetzULan::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    unsigned v;
    if (r.u("control", v)) m_control = (uint8_t)v;
    if (r.u("status", v)) m_status = (uint8_t)v;
    if (r.u("rdr", v)) m_rdr = (uint8_t)v;
    uint64_t n = 0;
    if (r.n64("rx_left", n)) m_rx_left = (int64_t)n;
    if (r.n64("tx_left", n)) m_tx_left = (int64_t)n;
    r.u("sent", m_sent);
    r.u("received", m_received);
    update_rate();
    m_rx_queue.clear();
    m_echo.clear();
    if (r.n64("rx_queued", n) && n > 0 && n <= 1024 * 1024) {
        std::vector<uint8_t> q(static_cast<size_t>(n), 0);
        if (r.array("rx_queue", q.data(), q.size())) m_rx_queue.assign(q.begin(), q.end());
    }
    if (r.n64("echo_queued", n) && n > 0 && n <= 1024 * 1024) {
        std::vector<uint8_t> q(static_cast<size_t>(n), 0);
        if (r.array("echo", q.data(), q.size())) m_echo.assign(q.begin(), q.end());
    }
    return emulator::Result::ok();
}

ComputerDevice * create_apple2_parallel(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new Apple2ParallelCard(im, cd);
}

ComputerDevice * create_pravetz_ulan(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PravetzULan(im, cd);
}

// ------------------------------ COMport server ------------------------------

ComportServer::ComportServer(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
}

emulator::Result ComportServer::load_image(unsigned drive, const std::string &file)
{
    std::string path = find_file_location(sd, file);
    if (path.empty()) path = file;
    if (!dsk_tools::file_exists(path))
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "File not found")) + "} " + file);
    const std::string data = dsk_tools::utf8_read_file(path);
    if (data.size() != IMAGE_SIZE)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "A 140K disk image in DOS order is expected")) + "} " + file);
    if (m_images.size() < drive) { m_images.resize(drive); m_files.resize(drive); }
    m_images[drive - 1].assign(data.begin(), data.end());
    m_files[drive - 1] = file;
    return emulator::Result::ok();
}

emulator::Result ComportServer::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;
    m_uart = dynamic_cast<INS8250*>(im->dm->get_device_by_name(cd->get_parameter("uart").value));
    if (m_uart == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "An ins8250 device is expected")) + "} " + name);
    // disk_1, disk_2, ...: the images the server starts with. A missing file
    // leaves the drive empty rather than stopping the machine
    for (unsigned n = 1; n <= 254; n++) {
        const std::string f = str_trim(cd->get_parameter("disk_" + std::to_string(n), false).value);
        if (f.empty()) continue;
        (void)load_image(n, f);
    }
    return emulator::Result::ok();
}

void ComportServer::answer(const std::vector<uint8_t> &bytes)
{
    for (uint8_t b : bytes) m_uart->peer_send(b);
}

void ComportServer::serial_in(uint8_t value)
{
    // Everything up to the sync byte is noise
    if (!m_in_frame) {
        if (value == 0xBF) { m_in_frame = true; m_frame.clear(); }
        return;
    }
    m_frame.push_back(value);
    const unsigned cmd = m_frame[0];
    const size_t need = (cmd == 2) ? 1 + 3 + 256 + 1 : 1 + 3 + 1;
    if (m_frame.size() < need) return;
    m_in_frame = false;
    request();
}

void ComportServer::request()
{
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < m_frame.size(); i++) sum = (uint8_t)(sum + m_frame[i]);
    if (sum != m_frame.back()) {
        m_errors++;
        answer({0xBF, 0xFF, 0xFF});
        return;
    }
    const unsigned cmd = m_frame[0], drive = m_frame[1], track = m_frame[2], sector = m_frame[3];
    m_last = (cmd << 24) | (drive << 16) | (track << 8) | sector;
    const bool there = drive >= 1 && drive <= m_images.size() && !m_images[drive - 1].empty()
                       && track < 35 && sector < 16 && (cmd == 1 || cmd == 2);
    if (!there) {
        m_errors++;
        answer({0xBF, 0x01, 0x01});
        return;
    }
    uint8_t * s = m_images[drive - 1].data() + (track * 16 + sector) * 256;
    if (cmd == 1) {
        std::vector<uint8_t> r = {0xBF, 0x00};
        uint8_t ds = 0;
        for (unsigned i = 0; i < 256; i++) { r.push_back(s[i]); ds = (uint8_t)(ds + s[i]); }
        r.push_back(ds);
        answer(r);
        m_reads++;
    } else {
        std::copy(m_frame.begin() + 4, m_frame.begin() + 4 + 256, s);
        answer({0xBF, 0x00, 0x00});
        m_writes++;
    }
}

// The images in the extension editor: a field disk_N for every disk_title_N
// of the config (the titles are the config's, like the jumpers of the
// Правец-8А), the file dialog filtered by the parameter files
ConfigFields ComportServer::get_config_fields()
{
    ConfigFields r;
    const std::string files = cd->get_parameter("files", false).value;
    for (unsigned n = 1; n <= 254; n++) {
        const std::string title = str_trim(cd->get_parameter("disk_title_" + std::to_string(n), false).value);
        if (title.empty()) continue;
        ConfigField f;
        f.name = "disk_" + std::to_string(n);
        f.title = title;
        f.type = CONFIG_FIELD_FILE;
        f.files = files;
        r.push_back(f);
    }
    return r;
}

std::vector<DeviceFieldInfo> ComportServer::get_device_fields()

{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"reads",  "Sectors read",                           false});
    r.push_back({"writes", "Sectors written",                        false});
    r.push_back({"errors", "Requests answered with an error",        false});
    r.push_back({"disks",  "Drives with an image: drive=file name, ...",  false});
    r.push_back({"last",   "The last request: command, drive, track, sector, a byte each", false});
    return r;
}

bool ComportServer::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "disks") {
        out.numeric = false;
        out.text.clear();
        for (size_t i = 0; i < m_images.size(); i++)
            if (!m_images[i].empty())
                {
                const size_t slash = m_files[i].find_last_of("/\\");
                out.text += (out.text.empty() ? "" : ", ") + std::to_string(i + 1) + "="
                          + (slash == std::string::npos ? m_files[i] : m_files[i].substr(slash + 1));
            }
        return true;
    }
    if (field == "reads" || field == "writes" || field == "errors" || field == "last") {
        out.numeric = true;
        out.width = 32;
        out.values.push_back(field == "reads" ? m_reads : field == "writes" ? m_writes : field == "errors" ? m_errors : m_last);
        return true;
    }

    return ComputerDevice::get_field(field, from, to, out);
}

std::vector<DeviceCommandInfo> ComportServer::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"load",  "drive, \"file\"", "Gives drive 1-254 a 140K image (DOS order)"});
    r.push_back({"save",  "drive, \"file\"", "Writes the image of a drive to a file"});
    r.push_back({"eject", "drive",           "Leaves a drive without an image"});
    return r;
}

emulator::Result ComportServer::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);
    if (command == "load" || command == "save" || command == "eject") {
        unsigned drive = 0;
        try { if (!p.empty()) drive = parse_numeric_value(p[0], 10); } catch (const std::exception &) { drive = 0; }
        if (drive < 1 || drive > 254 || (command != "eject" && p.size() < 2))
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "A drive 1-254 and a file are expected")) + "}");
        if (command == "load") return load_image(drive, p[1]);
        if (command == "eject") {
            if (drive <= m_images.size()) { m_images[drive - 1].clear(); m_files[drive - 1].clear(); }
            return emulator::Result::ok();
        }
        if (drive > m_images.size() || m_images[drive - 1].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "The drive has no image")) + "}");
        dsk_tools::UTF8_ofstream file(p[1], std::ios::binary);
        if (!file.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{Apple2Cards|" + std::string(QT_TRANSLATE_NOOP("Apple2Cards", "Cannot write the file")) + "} " + p[1]);
        file.write(reinterpret_cast<const char*>(m_images[drive - 1].data()), m_images[drive - 1].size());
        return emulator::Result::ok();
    }
    return ComputerDevice::send_command(command, parameters);
}

// The images are the server's disks: a snapshot carries them whole
void ComportServer::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);
    w.n("reads", m_reads);
    w.n("writes", m_writes);
    w.n("errors", m_errors);
    w.n("drives", (unsigned)m_images.size());
    for (size_t i = 0; i < m_images.size(); i++) {
        if (m_images[i].empty()) continue;
        const std::string k = "disk_" + std::to_string(i + 1);
        w.s((k + "_file").c_str(), m_files[i]);
        w.array(k.c_str(), m_images[i].data(), m_images[i].size(), 64);
    }
    w.b("in_frame", m_in_frame);
    w.n("frame_len", (unsigned)m_frame.size());
    if (!m_frame.empty()) w.array("frame", m_frame.data(), m_frame.size());
}

emulator::Result ComportServer::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;
    r.u("reads", m_reads);
    r.u("writes", m_writes);
    r.u("errors", m_errors);
    unsigned drives = 0;
    r.u("drives", drives);
    if (drives > 254) drives = 254;
    m_images.assign(drives, std::vector<uint8_t>());
    m_files.assign(drives, std::string());
    for (unsigned i = 0; i < drives; i++) {
        const std::string k = "disk_" + std::to_string(i + 1);
        std::vector<uint8_t> img(IMAGE_SIZE, 0);
        if (!r.array(k.c_str(), img.data(), img.size())) continue;
        m_images[i].swap(img);
        r.s((k + "_file").c_str(), m_files[i]);
    }
    r.b("in_frame", m_in_frame);
    unsigned len = 0;
    r.u("frame_len", len);
    m_frame.assign(len <= 300 ? len : 0, 0);
    if (!m_frame.empty()) r.array("frame", m_frame.data(), m_frame.size());
    return emulator::Result::ok();
}

ComputerDevice * create_comport_server(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new ComportServer(im, cd);
}
