// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ inter-processor channels

#include "uknc_channels.h"
#include "emulator/utils.h"

// Word register indices, see the table in the header
#define R_CPU_RX0_ST    0
#define R_CPU_RX0_DT    1
#define R_CPU_TX0_ST    2
#define R_CPU_TX0_DT    3
#define R_CPU_RX1_ST    4
#define R_CPU_RX1_DT    5
#define R_CPU_TX1_ST    6
#define R_CPU_TX1_DT    7
#define R_CPU_TX2_ST    8
#define R_CPU_TX2_DT    9
#define R_PPU_RX0_DT    10
#define R_PPU_RX1_DT    11
#define R_PPU_RX2_DT    12
#define R_PPU_RX_ST     13
#define R_PPU_TX0_DT    14
#define R_PPU_TX1_DT    15
#define R_PPU_UNUSED    16
#define R_PPU_TX_ST     17
#define R_COUNT         18

// Bits of a status register on the central processor's side
#define ST_READY        0200        // bit 7, read only
#define ST_IRQ_ENABLE   0100        // bit 6

UKNCChannels::UKNCChannels(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_cpu_virq(this, im, 1, "cpu_virq", MODE_W)
    , i_cpu_vector(this, im, 16, "cpu_vector", MODE_W)
    , i_ppu_virq(this, im, 1, "ppu_virq", MODE_W)
    , i_ppu_vector(this, im, 16, "ppu_vector", MODE_W)
{
    can_read = true;
    can_write = true;
    addresable_size = R_COUNT * 2;
}

emulator::Result UKNCChannels::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    // Defaults are the vectors of the real machine, so a config normally says
    // nothing about them at all
    static const unsigned int def_cpu_rx[UKNC_CHAN_P2C] = {0060, 0460};
    static const unsigned int def_cpu_tx[UKNC_CHAN_C2P] = {0064, 0464, 0474};
    static const unsigned int def_ppu_rx[UKNC_CHAN_C2P] = {0320, 0330, 0340};
    static const unsigned int def_ppu_tx[UKNC_CHAN_P2C] = {0324, 0334};

    for (unsigned int i = 0; i < UKNC_CHAN_P2C; i++) {
        m_vec_cpu_rx[i] = read_confg_value(cd, "cpu_rx_vector" + std::to_string(i), false, def_cpu_rx[i]);
        m_vec_ppu_tx[i] = read_confg_value(cd, "ppu_tx_vector" + std::to_string(i), false, def_ppu_tx[i]);
    }
    for (unsigned int i = 0; i < UKNC_CHAN_C2P; i++) {
        m_vec_cpu_tx[i] = read_confg_value(cd, "cpu_tx_vector" + std::to_string(i), false, def_cpu_tx[i]);
        m_vec_ppu_rx[i] = read_confg_value(cd, "ppu_rx_vector" + std::to_string(i), false, def_ppu_rx[i]);
    }

    return emulator::Result::ok();
}

void UKNCChannels::reset(MAYBE_UNUSED bool cold)
{
    for (unsigned int i = 0; i < UKNC_CHAN_C2P; i++) {
        m_c2p[i].data = 0;
        m_c2p[i].rx_ready = false;
        m_c2p[i].rx_irq = false;
        // A sender may write straight away: nothing is in the way yet
        m_c2p[i].tx_ready = true;
        m_c2p[i].tx_irq = false;
    }
    for (unsigned int i = 0; i < UKNC_CHAN_P2C; i++) {
        m_p2c[i].data = 0;
        m_p2c[i].rx_ready = false;
        m_p2c[i].rx_irq = false;
        m_p2c[i].tx_ready = true;
        m_p2c[i].tx_irq = false;
    }
    m_sent_c2p = 0;
    m_sent_p2c = 0;
    m_cpu_offered = 0;
    m_ppu_offered = 0;
    update_irq();
}

//--------------------------- The handshake --------------------------------//

void UKNCChannels::write_pipe(Pipe &p, unsigned int value)
{
    // The byte is handed over: the receiver now has something, the sender has
    // to wait until it is taken
    p.data = value & 0xFF;
    p.rx_ready = true;
    p.tx_ready = false;
}

unsigned int UKNCChannels::read_pipe(Pipe &p)
{
    // Taking the byte frees the channel and lets the sender fill it again
    p.rx_ready = false;
    p.tx_ready = true;
    return p.data & 0xFF;
}

//--------------------------- Status registers -----------------------------//

unsigned int UKNCChannels::cpu_status(const Pipe &p, bool receiver) const
{
    const bool ready = receiver? p.rx_ready : p.tx_ready;
    const bool irq   = receiver? p.rx_irq   : p.tx_irq;
    return (ready? ST_READY : 0) | (irq? ST_IRQ_ENABLE : 0);
}

void UKNCChannels::set_cpu_status(Pipe &p, bool receiver, unsigned int value)
{
    // Only the interrupt enable is writable; READY belongs to the hardware and
    // a program that writes it back must not be able to invent a byte
    if (receiver) p.rx_irq = (value & ST_IRQ_ENABLE) != 0;
    else          p.tx_irq = (value & ST_IRQ_ENABLE) != 0;
}

// The peripheral processor sees all of its channels in one register: bits 5, 4
// and 3 are the ready flags of channels 2, 1 and 0, bits 2, 1 and 0 their
// interrupt enables
unsigned int UKNCChannels::ppu_rx_status() const
{
    unsigned int v = 0;
    for (unsigned int i = 0; i < UKNC_CHAN_C2P; i++) {
        if (m_c2p[i].rx_ready) v |= (1u << (3 + i));
        if (m_c2p[i].rx_irq)   v |= (1u << i);
    }
    return v;
}

void UKNCChannels::set_ppu_rx_status(unsigned int value)
{
    for (unsigned int i = 0; i < UKNC_CHAN_C2P; i++)
        m_c2p[i].rx_irq = (value & (1u << i)) != 0;
}

unsigned int UKNCChannels::ppu_tx_status() const
{
    unsigned int v = 0;
    for (unsigned int i = 0; i < UKNC_CHAN_P2C; i++) {
        if (m_p2c[i].tx_ready) v |= (1u << (3 + i));
        if (m_p2c[i].tx_irq)   v |= (1u << i);
    }
    return v;
}

void UKNCChannels::set_ppu_tx_status(unsigned int value)
{
    for (unsigned int i = 0; i < UKNC_CHAN_P2C; i++)
        m_p2c[i].tx_irq = (value & (1u << i)) != 0;
}

//--------------------------- Interrupt lines ------------------------------//

void UKNCChannels::update_irq()
{
    // Lowest channel first, receiver before sender - the order the devices sit
    // on the bus. Only one vector can be offered at a time, the rest wait
    unsigned int cpu_vector = 0;
    for (unsigned int i = 0; i < UKNC_CHAN_P2C && cpu_vector == 0; i++)
        if (m_p2c[i].rx_ready && m_p2c[i].rx_irq) cpu_vector = m_vec_cpu_rx[i];
    for (unsigned int i = 0; i < UKNC_CHAN_C2P && cpu_vector == 0; i++)
        if (m_c2p[i].tx_ready && m_c2p[i].tx_irq) cpu_vector = m_vec_cpu_tx[i];

    unsigned int ppu_vector = 0;
    for (unsigned int i = 0; i < UKNC_CHAN_C2P && ppu_vector == 0; i++)
        if (m_c2p[i].rx_ready && m_c2p[i].rx_irq) ppu_vector = m_vec_ppu_rx[i];
    for (unsigned int i = 0; i < UKNC_CHAN_P2C && ppu_vector == 0; i++)
        if (m_p2c[i].tx_ready && m_p2c[i].tx_irq) ppu_vector = m_vec_ppu_tx[i];

    // The vector goes out before the request: the processor samples ~vector at
    // the moment ~virq becomes active.
    //
    // One wire carries several sources, and the processor takes the request on
    // the edge, so a line that is already down delivers nothing new. When the
    // source changes - one channel drained and the next one waiting - the line
    // is released and pulled again to make that edge. While the same source
    // stays pending the line is left alone: a handler that does not drain its
    // channel then simply does not get called again, instead of re-entering
    // itself until the stack runs off the bottom of memory
    if (cpu_vector != m_cpu_offered) {
        if (cpu_vector != 0) {
            i_cpu_vector.change(cpu_vector);
            i_cpu_virq.change(1);
            i_cpu_virq.change(0);
        } else
            i_cpu_virq.change(1);
        m_cpu_offered = cpu_vector;
    }

    if (ppu_vector != m_ppu_offered) {
        if (ppu_vector != 0) {
            i_ppu_vector.change(ppu_vector);
            i_ppu_virq.change(1);
            i_ppu_virq.change(0);
        } else
            i_ppu_virq.change(1);
        m_ppu_offered = ppu_vector;
    }
}

//--------------------------- Bus access -----------------------------------//

unsigned int UKNCChannels::get_value_word(unsigned int address)
{
    const unsigned int reg = (address >> 1);
    unsigned int v = 0;

    switch (reg) {
    case R_CPU_RX0_ST: v = cpu_status(m_p2c[0], true);  break;
    case R_CPU_TX0_ST: v = cpu_status(m_c2p[0], false); break;
    case R_CPU_RX1_ST: v = cpu_status(m_p2c[1], true);  break;
    case R_CPU_TX1_ST: v = cpu_status(m_c2p[1], false); break;
    case R_CPU_TX2_ST: v = cpu_status(m_c2p[2], false); break;

    // Reading a data register takes the byte, which frees the channel
    case R_CPU_RX0_DT: v = read_pipe(m_p2c[0]); update_irq(); break;
    case R_CPU_RX1_DT: v = read_pipe(m_p2c[1]); update_irq(); break;
    case R_PPU_RX0_DT: v = read_pipe(m_c2p[0]); update_irq(); break;
    case R_PPU_RX1_DT: v = read_pipe(m_c2p[1]); update_irq(); break;
    case R_PPU_RX2_DT: v = read_pipe(m_c2p[2]); update_irq(); break;

    // A sender reading back its own data register sees what it wrote; it does
    // not disturb the handshake
    case R_CPU_TX0_DT: v = m_c2p[0].data; break;
    case R_CPU_TX1_DT: v = m_c2p[1].data; break;
    case R_CPU_TX2_DT: v = m_c2p[2].data; break;
    case R_PPU_TX0_DT: v = m_p2c[0].data; break;
    case R_PPU_TX1_DT: v = m_p2c[1].data; break;

    case R_PPU_RX_ST:  v = ppu_rx_status(); break;
    case R_PPU_TX_ST:  v = ppu_tx_status(); break;

    default: v = 0; break;
    }

    return v & 0xFFFF;
}

void UKNCChannels::set_value_word(unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    const unsigned int reg = (address >> 1);

    switch (reg) {
    case R_CPU_RX0_ST: set_cpu_status(m_p2c[0], true,  value); update_irq(); break;
    case R_CPU_TX0_ST: set_cpu_status(m_c2p[0], false, value); update_irq(); break;
    case R_CPU_RX1_ST: set_cpu_status(m_p2c[1], true,  value); update_irq(); break;
    case R_CPU_TX1_ST: set_cpu_status(m_c2p[1], false, value); update_irq(); break;
    case R_CPU_TX2_ST: set_cpu_status(m_c2p[2], false, value); update_irq(); break;

    // Writing a data register hands the byte to the other processor
    case R_CPU_TX0_DT: write_pipe(m_c2p[0], value); m_sent_c2p++; update_irq(); break;
    case R_CPU_TX1_DT: write_pipe(m_c2p[1], value); m_sent_c2p++; update_irq(); break;
    case R_CPU_TX2_DT: write_pipe(m_c2p[2], value); m_sent_c2p++; update_irq(); break;
    case R_PPU_TX0_DT: write_pipe(m_p2c[0], value); m_sent_p2c++; update_irq(); break;
    case R_PPU_TX1_DT: write_pipe(m_p2c[1], value); m_sent_p2c++; update_irq(); break;

    case R_PPU_RX_ST:  set_ppu_rx_status(value); update_irq(); break;
    case R_PPU_TX_ST:  set_ppu_tx_status(value); update_irq(); break;

    // A receiver's data register is read only, and so is READY in a status one
    default: break;
    }
}

unsigned int UKNCChannels::get_value(unsigned int address)
{
    // Byte access picks a lane. The low byte of a data register is the byte
    // itself, which is how the ROM reads these registers
    const unsigned int w = get_value_word(address & ~1u);
    return (address & 1)? ((w >> 8) & 0xFF) : (w & 0xFF);
}

void UKNCChannels::set_value(unsigned int address, unsigned int value, bool force)
{
    if (address & 1) {
        // The high lane carries nothing these registers use
        return;
    }
    set_value_word(address, value & 0xFF, force);
}

//--------------------------- Introspection --------------------------------//

std::vector<DeviceFieldInfo> UKNCChannels::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"channels", "State of every channel: data and handshake", false});
    r.push_back({"sent_c2p", "Bytes handed from the central processor to the peripheral", false});
    r.push_back({"sent_p2c", "Bytes handed from the peripheral processor to the central", false});
    r.push_back({"cpu_vector", "Vector currently offered to the central processor", false});
    r.push_back({"ppu_vector", "Vector currently offered to the peripheral processor", false});
    return r;
}

bool UKNCChannels::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "sent_c2p") {
        out.numeric = true; out.width = 16;
        out.values.push_back(m_sent_c2p);
        return true;
    }
    if (field == "sent_p2c") {
        out.numeric = true; out.width = 16;
        out.values.push_back(m_sent_p2c);
        return true;
    }
    if (field == "cpu_vector") {
        out.numeric = true; out.width = 16;
        out.values.push_back((i_cpu_virq.value & 1) == 0 ? i_cpu_vector.value : 0);
        return true;
    }
    if (field == "ppu_vector") {
        out.numeric = true; out.width = 16;
        out.values.push_back((i_ppu_virq.value & 1) == 0 ? i_ppu_vector.value : 0);
        return true;
    }

    // The one line that answers "why is the handshake stuck"
    if (field == "channels") {
        out.numeric = false;
        for (unsigned int i = 0; i < UKNC_CHAN_C2P; i++) {
            out.text += "        ЦП->ПП " + std::to_string(i)
                      + ": data=" + oct_str(m_c2p[i].data, 3)
                      + " rx_ready=" + (m_c2p[i].rx_ready? "1" : "0")
                      + " rx_irq="   + (m_c2p[i].rx_irq?   "1" : "0")
                      + " tx_ready=" + (m_c2p[i].tx_ready? "1" : "0")
                      + " tx_irq="   + (m_c2p[i].tx_irq?   "1" : "0") + "\n";
        }
        for (unsigned int i = 0; i < UKNC_CHAN_P2C; i++) {
            out.text += "        ПП->ЦП " + std::to_string(i)
                      + ": data=" + oct_str(m_p2c[i].data, 3)
                      + " rx_ready=" + (m_p2c[i].rx_ready? "1" : "0")
                      + " rx_irq="   + (m_p2c[i].rx_irq?   "1" : "0")
                      + " tx_ready=" + (m_p2c[i].tx_ready? "1" : "0")
                      + " tx_irq="   + (m_p2c[i].tx_irq?   "1" : "0");
            if (i + 1 < UKNC_CHAN_P2C) out.text += "\n";
        }
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_uknc_channels(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UKNCChannels(im, cd);
}
