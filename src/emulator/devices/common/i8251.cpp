// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8251 (КР580ВВ51) programmable communication interface (USART)

#include "i8251.h"

// Status register bits
#define STATUS_TXRDY    0x01    // Transmitter ready
#define STATUS_RXRDY    0x02    // Receiver ready
#define STATUS_TXE      0x04    // Transmitter empty
#define STATUS_PE       0x08    // Parity error
#define STATUS_OE       0x10    // Overrun error
#define STATUS_FE       0x20    // Framing error
#define STATUS_SYNDET   0x40    // Sync detect / break detect
#define STATUS_DSR      0x80    // Data Set Ready (active low input, active high in status)

// Command word bits
#define CMD_TXEN        0x01    // Transmit enable
#define CMD_DTR         0x02    // Data Terminal Ready
#define CMD_RXEN        0x04    // Receive enable
#define CMD_SBRK        0x08    // Send break
#define CMD_ER          0x10    // Error reset
#define CMD_RTS         0x20    // Request To Send
#define CMD_IR          0x40    // Internal reset
#define CMD_EH          0x80    // Enter hunt mode (sync only)

// Callback IDs
#define CALLBACK_RXD    1
#define CALLBACK_DSR    2
#define CALLBACK_CTS    3
#define CALLBACK_TXC    4
#define CALLBACK_RXC    5
#define CALLBACK_C      6

I8251::I8251(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 1, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_txrdy(this, im, 1, "txrdy", MODE_W)
    , i_rxrdy(this, im, 1, "rxrdy", MODE_W)
    , i_txe(this, im, 1, "txe", MODE_W)
    , i_dtr(this, im, 1, "dtr", MODE_W)
    , i_rts(this, im, 1, "rts", MODE_W)
    , i_dsr(this, im, 1, "dsr", MODE_R, CALLBACK_DSR)
    , i_cts(this, im, 1, "cts", MODE_R, CALLBACK_CTS)
    , i_syndet(this, im, 1, "syndet", MODE_R)
    , i_txd(this, im, 8, "txd", MODE_W)
    , i_rxd(this, im, 8, "rxd", MODE_R, CALLBACK_RXD)
    , i_txc(this, im, 1, "txc", MODE_R, CALLBACK_TXC)
    , i_rxc(this, im, 1, "rxc", MODE_R, CALLBACK_RXC)
    , i_c(this, im, 1, "c", MODE_R, CALLBACK_C)
    , ext_clock(false)
{
    m_clocked = true;   //clock() is overridden here
    init();
}

//Кто тактирует передатчик. Линия подведена - считать его такты системным
//клоком нельзя: символ уходил бы за несколько машинных циклов вместо своих
//восьми битовых интервалов
emulator::Result I8251::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    ext_clock = (i_txc.linked > 0) || (i_c.linked > 0);

    return emulator::Result::ok();
}

void I8251::reset(const bool cold)
{
    AddressableDevice::reset(cold);
    init();
}

void I8251::init()
{
    mode_word = 0;
    sync_mode = false;
    baud_factor = 1;
    char_length = 8;
    parity_enable = false;
    even_parity = false;
    stop_bits = 1;
    sync_chars[0] = 0;
    sync_chars[1] = 0;
    single_sync = false;

    command_word = 0;
    tx_enable = false;
    rx_enable = false;
    send_break = false;
    hunt_mode = false;

    status = STATUS_TXE;
    control_state = STATE_MODE;
    sync_chars_remaining = 0;

    tx_buffer = 0;
    rx_buffer = 0;
    tx_buffer_full = false;
    rx_buffer_full = false;

    tx_clock_count = 0;
    rx_clock_count = 0;
    tx_clock_divider = 1;
    rx_clock_divider = 1;

    i_txrdy.change(0);
    i_rxrdy.change(0);
    i_txe.change(1);
    i_dtr.change(0);
    i_rts.change(0);
}

//Сколько тактов передатчика уходит на символ. В синхронном режиме это его
//разряды (плюс четность), в асинхронном к ним добавляются старт и стоп, и
//каждый разряд идет baud_factor тактов. Раньше символ уходил за один такт,
//то есть в восемь-одиннадцать раз быстрее живой микросхемы: запись на ленту
//Юниора укладывалась в миллисекунды вместо секунд, и услышать ее было нечем
void I8251::set_char_clocks()
{
    unsigned int bits = char_length + (parity_enable?1u:0u);
    if (!sync_mode) bits += 1 + ((stop_bits >= 3)?2u:1u);
    tx_clock_divider = bits * baud_factor;
    if (tx_clock_divider < 1) tx_clock_divider = 1;
    rx_clock_divider = tx_clock_divider;
}

void I8251::parse_mode_word(uint8_t value)
{
    mode_word = value;

    if ((value & 0x03) == 0) {
        // Synchronous mode
        sync_mode = true;
        baud_factor = 1;
        single_sync = (value & 0x80) != 0;

        char_length = 5 + ((value >> 2) & 0x03);
        parity_enable = (value & 0x10) != 0;
        even_parity = (value & 0x20) != 0;

        set_char_clocks();

        // Need to receive sync characters next
        sync_chars_remaining = single_sync ? 1 : 2;
        control_state = STATE_SYNC_CHAR;
    } else {
        // Asynchronous mode
        sync_mode = false;

        switch (value & 0x03) {
            case 1: baud_factor = 1;  break;
            case 2: baud_factor = 16; break;
            case 3: baud_factor = 64; break;
        }

        char_length = 5 + ((value >> 2) & 0x03);
        parity_enable = (value & 0x10) != 0;
        even_parity = (value & 0x20) != 0;

        switch ((value >> 6) & 0x03) {
            case 0:  stop_bits = 0; break; // Invalid
            case 1:  stop_bits = 1; break; // 1 stop bit
            case 2:  stop_bits = 2; break; // 1.5 stop bits
            case 3:  stop_bits = 3; break; // 2 stop bits
        }

        set_char_clocks();

        control_state = STATE_COMMAND;
    }
}

void I8251::update_command(uint8_t value)
{
    command_word = value;

    // Internal reset
    if (value & CMD_IR) {
        init();
        return;
    }

    tx_enable = (value & CMD_TXEN) != 0;
    rx_enable = (value & CMD_RXEN) != 0;
    send_break = (value & CMD_SBRK) != 0;

    // DTR and RTS outputs (active low on real hardware, but we use active high logic)
    i_dtr.change((value & CMD_DTR) ? 1 : 0);
    i_rts.change((value & CMD_RTS) ? 1 : 0);

    // Error reset
    if (value & CMD_ER) {
        status &= ~(STATUS_PE | STATUS_OE | STATUS_FE);
    }

    // Enter hunt mode (sync only)
    if ((value & CMD_EH) && sync_mode) {
        hunt_mode = true;
        sync_second_seen = false;
        status &= ~STATUS_SYNDET;
        i_syndet.change(0);
    }

    update_status();
}

void I8251::update_status()
{
    // TxRDY: transmitter is ready to accept data
    if (tx_enable && !tx_buffer_full) {
        status |= STATUS_TXRDY;
        i_txrdy.change(1);
    } else {
        status &= ~STATUS_TXRDY;
        i_txrdy.change(0);
    }

    // TxE: transmitter is completely empty
    if (!tx_buffer_full) {
        status |= STATUS_TXE;
        i_txe.change(1);
    } else {
        status &= ~STATUS_TXE;
        i_txe.change(0);
    }

    // RxRDY: receiver has data available
    if (rx_buffer_full) {
        status |= STATUS_RXRDY;
        i_rxrdy.change(1);
    } else {
        status &= ~STATUS_RXRDY;
        i_rxrdy.change(0);
    }

    // DSR input (directly reflected in status)
    if ((i_dsr.value & 1) == 0) {
        status |= STATUS_DSR;  // DSR is active low, shown as active high in status
    } else {
        status &= ~STATUS_DSR;
    }
}

unsigned int I8251::get_value(unsigned int address)
{
    // std::cout << "I8251::get_value " << address << std::endl;
    if ((address & 1) == 0) {
        // A0=0: Read data
        rx_buffer_full = false;
        update_status();
        return rx_buffer;
    } else {
        // A0=1: Read status
        update_status();
        const uint8_t v = status;
        //SYNDET снимается чтением статуса (паспорт микросхемы), иначе ПЗУ
        //Юниора приняло бы одну засечку синхронизации за все последующие
        status &= ~STATUS_SYNDET;
        if ((v & STATUS_SYNDET) != 0) i_syndet.change(0);
        return v;
    }
}

unsigned I8251::get_direct(unsigned address)
{
    //Reading the data register takes the character out of the receiver; an
    //inspection must leave it there for the program to find
    if ((address & 1) == 0)
        return rx_buffer;
    else
        return status;
}

void I8251::set_value(const unsigned address, const unsigned value, bool force)
{
    // std::cout << "I8251::set_value " << address << " 0x" << std::hex << (value & 0xFF) << std::endl;
    if ((address & 1) == 0) {
        // A0=0: Write data
        tx_buffer = value & 0xFF;
        tx_buffer_full = true;
        update_status();
    } else {
        // A0=1: Write control
        switch (control_state) {
            case STATE_MODE:
                parse_mode_word(value & 0xFF);
                break;
            case STATE_SYNC_CHAR: {
                //Индекс считается от того, сколько символов вообще ожидается:
                //при одиночной синхронизации единственный символ - нулевой, а
                //не первый. Раньше он ложился в sync_chars[1], и приемник искал
                //в потоке совсем не тот байт
                const unsigned int total = single_sync?1:2;
                sync_chars[total - sync_chars_remaining] = value & 0xFF;
                sync_chars_remaining--;
                if (sync_chars_remaining == 0)
                    control_state = STATE_COMMAND;
                break;
            }
            case STATE_COMMAND:
                update_command(value & 0xFF);
                break;
        }
    }
}

void I8251::do_clock(const unsigned counter)
{
    if (tx_enable && tx_buffer_full) {
        tx_clock_count += counter;
        if (tx_clock_count >= tx_clock_divider) {
            tx_clock_count -= tx_clock_divider;
            i_txd.change(tx_buffer);
            tx_buffer_full = false;
            update_status();
        }
    }
}

void I8251::clock(const unsigned counter)
{
    if (!ext_clock)
        do_clock(counter);
}

void I8251::interface_callback(MAYBE_UNUSED unsigned callback_id, const unsigned new_value, const unsigned old_value)
{
    if (callback_id == CALLBACK_RXD) {
        // Data received on rxd interface
        if (rx_enable) {
            // В синхронном режиме приемник сначала ищет синхросимвол и все, что
            // до него, выбрасывает. Юниор на этом и держится: лента идет
            // сплошным потоком, началом записи считается байт $E6, а ПЗУ ждет
            // SYNDET, прежде чем брать данные (FBB3)
            if (sync_mode && hunt_mode) {
                const uint8_t v = (uint8_t)(new_value & 0xFF);
                if (v == sync_chars[0]) {
                    if (single_sync || sync_second_seen) {
                        hunt_mode = false;
                        sync_second_seen = false;
                        status |= STATUS_SYNDET;
                        i_syndet.change(1);
                    } else {
                        // Двойной синхросимвол: первый только что пришел
                        sync_second_seen = true;
                    }
                } else
                if (!single_sync && sync_second_seen && v == sync_chars[1]) {
                    hunt_mode = false;
                    sync_second_seen = false;
                    status |= STATUS_SYNDET;
                    i_syndet.change(1);
                } else {
                    sync_second_seen = false;
                }
                //Сам синхросимвол в буфер не попадает
                return;
            }
            if (rx_buffer_full) {
                // Overrun error
                status |= STATUS_OE;
            }
            rx_buffer = new_value & 0xFF;
            rx_buffer_full = true;
            update_status();
        }
    } else if (callback_id == CALLBACK_DSR) {
        update_status();
    } else if (callback_id == CALLBACK_CTS) {
        update_status();
    } else if (callback_id == CALLBACK_TXC) {
        // Transmitter clock edge (rising)
        if ((new_value & 1) && !(old_value & 1)) {
            if (tx_enable && tx_buffer_full) {
                tx_clock_count++;
                if (tx_clock_count >= tx_clock_divider) {
                    tx_clock_count = 0;
                    i_txd.change(tx_buffer);
                    tx_buffer_full = false;
                    update_status();
                }
            }
        }
    } else if (callback_id == CALLBACK_RXC) {
        // Receiver clock edge (rising)
        if ((new_value & 1) && !(old_value & 1)) {
            // Receiver clocking handled via rxd callback for simplicity
        }
    } else if (callback_id == CALLBACK_C) {
        ext_clock = true;
        // Rising edge on external clock
        if ((new_value & 1) && !(old_value & 1)) {
            do_clock(1);
        }
    }
}

void I8251::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.u("mode_word", mode_word, 8);
    w.u("command_word", command_word, 8);
    w.n("control_state", static_cast<uint32_t>(control_state));
    w.n("sync_chars_remaining", sync_chars_remaining);
    w.array("sync_chars", sync_chars, 2);
    w.u("status", status, 8);
    w.u("tx_buffer", tx_buffer, 8);
    w.u("rx_buffer", rx_buffer, 8);
    w.b("tx_buffer_full", tx_buffer_full);
    w.b("rx_buffer_full", rx_buffer_full);
    w.b("tx_enable", tx_enable);
    w.b("rx_enable", rx_enable);
    w.b("send_break", send_break);
    w.b("hunt_mode", hunt_mode);
    w.b("sync_second_seen", sync_second_seen);
    //Whether anything is driving the C line. Not derivable from the line's
    //value - it is set by the first edge that ever arrives - so it has to
    //travel with the state, or a restored chip clocks itself instead
    w.b("ext_clock", ext_clock);
    //Where the shift clocks stand: a character takes a number of them
    w.n("tx_clock_count", tx_clock_count);
    w.n("rx_clock_count", rx_clock_count);
}

emulator::Result I8251::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    //Everything the mode word means - the baud factor, the character length,
    //parity, the stop bits - is worked out when the word is written and kept
    //in members of its own. Restoring the word alone left those at what init()
    //gave them, and a chip programmed for x16 shifted characters sixteen times
    //too fast. parse_mode_word() also moves control_state and the sync counter,
    //which the two reads below then put back where the snapshot had them
    if (r.u("mode_word", mode_word)) parse_mode_word(static_cast<uint8_t>(mode_word));
    r.u("command_word", command_word);
    uint32_t state = static_cast<uint32_t>(control_state);
    if (r.u("control_state", state)) control_state = static_cast<ControlState>(state);
    r.u("sync_chars_remaining", sync_chars_remaining);
    r.array("sync_chars", sync_chars, 2);
    r.u("status", status);
    r.u("tx_buffer", tx_buffer);
    r.u("rx_buffer", rx_buffer);
    r.b("tx_buffer_full", tx_buffer_full);
    r.b("rx_buffer_full", rx_buffer_full);
    r.b("tx_enable", tx_enable);
    r.b("rx_enable", rx_enable);
    r.b("send_break", send_break);
    r.b("hunt_mode", hunt_mode);
    r.b("sync_second_seen", sync_second_seen);
    r.b("ext_clock", ext_clock);
    r.u("tx_clock_count", tx_clock_count);
    r.u("rx_clock_count", rx_clock_count);
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> I8251::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"status",  "Status register, raw",                         false});
    r.push_back({"flags",   "Status register decoded as name=value pairs",  false});
    r.push_back({"mode",    "Mode word, raw",                               false});
    r.push_back({"format",  "Character format decoded from the mode word",  false});
    r.push_back({"command", "Command word, raw",                            false});
    r.push_back({"tx",      "Byte in the transmit buffer",                  false});
    r.push_back({"rx",      "Byte in the receive buffer",                   false});
    r.push_back({"buffers", "Which of the two buffers are full",            false});
    r.push_back({"state",   "What the control port expects to be written next", false});
    return r;
}

bool I8251::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "status" || field == "mode" || field == "command" ||
        field == "tx" || field == "rx")
    {
        out.numeric = true;
        if (field == "status")       out.values.push_back(status);
        else if (field == "mode")    out.values.push_back(mode_word);
        else if (field == "command") out.values.push_back(command_word);
        else if (field == "tx")      out.values.push_back(tx_buffer);
        else                         out.values.push_back(rx_buffer);
        return true;
    }

    if (field == "flags")
    {
        out.numeric = false;
        out.text =
            std::string("TXRDY=") + ((status & STATUS_TXRDY)  ? "1" : "0")
                     + " RXRDY="  + ((status & STATUS_RXRDY)  ? "1" : "0")
                     + " TXE="    + ((status & STATUS_TXE)    ? "1" : "0")
                     + " PE="     + ((status & STATUS_PE)     ? "1" : "0")
                     + " OE="     + ((status & STATUS_OE)     ? "1" : "0")
                     + " FE="     + ((status & STATUS_FE)     ? "1" : "0")
                     + " SYNDET=" + ((status & STATUS_SYNDET) ? "1" : "0")
                     + " DSR="    + ((status & STATUS_DSR)    ? "1" : "0");
        return true;
    }

    if (field == "format")
    {
        out.numeric = false;
        if (sync_mode)
            out.text = "sync, " + std::to_string(char_length) + " bits, "
                     + (parity_enable ? (even_parity ? "even parity" : "odd parity") : "no parity")
                     + ", " + (single_sync ? "1" : "2") + " sync chars";
        else
            //Half a stop bit is stored as the value 2, hence the odd looking table
            out.text = "async x" + std::to_string(baud_factor) + ", "
                     + std::to_string(char_length) + " bits, "
                     + (parity_enable ? (even_parity ? "even parity" : "odd parity") : "no parity")
                     + ", " + ((stop_bits == 1) ? "1" : (stop_bits == 2) ? "1.5" : "2") + " stop";
        out.text += std::string("; TXEN=") + (tx_enable ? "1" : "0")
                  + " RXEN=" + (rx_enable ? "1" : "0")
                  + (send_break ? " BREAK" : "")
                  + (hunt_mode ? " HUNT" : "");
        return true;
    }

    if (field == "buffers")
    {
        out.numeric = false;
        out.text = std::string("tx=") + (tx_buffer_full ? "full" : "empty")
                 + " rx=" + (rx_buffer_full ? "full" : "empty");
        return true;
    }

    //Writing a command word while the chip still waits for the mode word is a
    //classic way to get a port that answers with nonsense
    if (field == "state")
    {
        out.numeric = false;
        switch (control_state)
        {
            case STATE_MODE:      out.text = "mode word expected"; break;
            case STATE_SYNC_CHAR: out.text = "sync character expected, "
                                           + std::to_string(sync_chars_remaining) + " to go"; break;
            default:              out.text = "command word expected"; break;
        }
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_i8251(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8251(im, cd);
}