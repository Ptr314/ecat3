// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: УК-НЦ inter-processor channels

#pragma once

#include "emulator/core.h"

// The five byte-wide channels between the two processors of the УК-НЦ: three
// from the central processor to the peripheral one and two back. Each is a
// single byte with a ready/interrupt handshake - there is no shared memory in
// the machine, so this and the indirect-access registers are the only way the
// processors talk.
//
// System software uses channel 0 as the terminal (the peripheral processor owns
// the screen and the keyboard, so all console traffic goes through it), channel
// 1 as the printer and channel 2 as a command channel for bulk transfers.
//
// One device serves both processors: it is mapped into both address spaces at
// different bases, since the two sides see different registers at different
// addresses. The register layout below is the device's own, and the config
// slices it up with @memory[...] = channels[base].
//
//   offset  ЦП                              offset  ПП
//   ------  ------------------------------  ------  ---------------------------
//   000     канал 0, приёмник, состояние    024     канал 0, приёмник, данные
//   002     канал 0, приёмник, данные       026     канал 1, приёмник, данные
//   004     канал 0, источник, состояние    030     канал 2, приёмник, данные
//   006     канал 0, источник, данные       032     приёмники, состояние
//   010     канал 1, приёмник, состояние    034     канал 0, источник, данные
//   012     канал 1, приёмник, данные       036     канал 1, источник, данные
//   014     канал 1, источник, состояние    040     (не используется)
//   016     канал 1, источник, данные       042     источники, состояние
//   020     канал 2, источник, состояние
//   022     канал 2, источник, данные
//
// A status register on the central processor's side carries READY in bit 7 and
// the interrupt enable in bit 6, one channel per register. The peripheral
// processor gets all of its channels in one register instead, which is why the
// two sides cannot share a format.

#define UKNC_CHAN_C2P   3       // central -> peripheral
#define UKNC_CHAN_P2C   2       // peripheral -> central

class UKNCChannels: public AddressableDevice
{
private:
    // One direction of one channel. The sender and the receiver each have a
    // ready flag and an interrupt enable of their own: a write hands the byte
    // over and wakes the receiver, a read takes it and wakes the sender.
    struct Pipe {
        unsigned int data;
        bool rx_ready;          // the receiver has a byte waiting
        bool rx_irq;            // the receiver wants an interrupt for it
        bool tx_ready;          // the sender may put the next byte in
        bool tx_irq;            // the sender wants an interrupt when it may
    };

    Pipe m_c2p[UKNC_CHAN_C2P];
    Pipe m_p2c[UKNC_CHAN_P2C];

    // Vectors each side is interrupted through, taken from the config
    unsigned int m_vec_cpu_rx[UKNC_CHAN_P2C];   // byte arrived from the peripheral
    unsigned int m_vec_cpu_tx[UKNC_CHAN_C2P];   // the peripheral took a byte
    unsigned int m_vec_ppu_rx[UKNC_CHAN_C2P];   // byte arrived from the central
    unsigned int m_vec_ppu_tx[UKNC_CHAN_P2C];   // the central took a byte

    // Request lines to the two processors. Active low, like every 1801 bus
    // signal: 0 means "a vector is waiting", and ~vector carries which one.
    Interface i_cpu_virq;
    Interface i_cpu_vector;
    Interface i_ppu_virq;
    Interface i_ppu_vector;

    // Counters, for scripts watching a handshake go wrong
    // Вектор, который уже предложен каждой стороне. Линия запроса одна на
    // несколько источников, и процессор слышит её по фронту, поэтому при
    // смене источника её надо отпустить и прижать заново - иначе из целой
    // очереди готовых каналов будет доставлен только первый
    unsigned int m_cpu_offered = 0;
    unsigned int m_ppu_offered = 0;

    unsigned int m_sent_c2p = 0;
    unsigned int m_sent_p2c = 0;

    void write_pipe(Pipe &p, unsigned int value);   // a byte handed over
    unsigned int read_pipe(Pipe &p);                // a byte taken

    void update_irq();                              // recomputes both request lines
    unsigned int cpu_status(const Pipe &p, bool receiver) const;
    unsigned int ppu_rx_status() const;
    unsigned int ppu_tx_status() const;
    void set_cpu_status(Pipe &p, bool receiver, unsigned int value);
    void set_ppu_rx_status(unsigned int value);
    void set_ppu_tx_status(unsigned int value);

public:
    UKNCChannels(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_uknc_channels(InterfaceManager *im, EmulatorConfigDevice *cd);
