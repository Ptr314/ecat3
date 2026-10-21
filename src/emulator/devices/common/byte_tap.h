// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The bytes a port sends out, kept for scripts and the ports window

#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "emulator/core.h"

// Что программа вывела в порт: последовательная линия (DL11) или принтер.
// Примесь, не устройство: устройство отдаёт сюда каждый байт, ушедший наружу
// (put), а отсюда его берут двое.
//
//   - Поля output и text сценариев и MCP: последние OUTPUT_SIZE байтов в
//     кольце. Его пишет и читает поток эмуляции.
//   - Окно «Порты ввода-вывода»: отвод (set_tap/take_tap) включает окно, и
//     пока он включён, байты копятся в буфере под мьютексом; окно забирает их
//     из потока GUI по таймеру - ни обратного вызова на байт, ни invoke().
//     Предел - на случай, если окно не забирает.
//
// Ни то, ни другое не состояние машины: снимок их не сохраняет.
class ByteTap
{
public:
    virtual ~ByteTap() = default;

    // Для окна портов, из потока GUI: включить отвод и забрать накопленное
    void set_tap(bool on);
    void take_tap(std::vector<uint8_t> &out);

protected:
    // Байт ушёл наружу
    void put(uint8_t b);

    // Поля output и text: true, если field - одно из них
    bool get_output_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) const;
    // Их описания, для get_device_fields()
    static void add_output_fields(std::vector<DeviceFieldInfo> &r);

private:
    static const unsigned int OUTPUT_SIZE = 1024;
    uint8_t m_output[OUTPUT_SIZE] = {};
    unsigned int m_put = 0;             // сколько байтов прошло через кольцо

    static const size_t TAP_LIMIT = 1 << 20;
    std::atomic<bool> m_tap_on{false};
    compat_mutex m_tap_mutex;
    std::vector<uint8_t> m_tap;
};
