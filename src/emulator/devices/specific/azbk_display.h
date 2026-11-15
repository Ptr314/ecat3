// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the VGA picture, header

#pragma once

#include <cstdint>
#include <vector>

#include "emulator/core.h"

class AZBKVga;

// Картина VGA контроллера AZBK, 1024x768. Строки рисует развертка azbk-vga, в
// начале каждой видимой строки, по регистрам, как они стоят для нее; конец
// строки 767 - кадр готов.
//
// Строка 2-8 бит на точку с повтором точки x1-x8 и строки x1-x4; страница
// начала (4 КБ) и номер строки рулона дают адрес, с него 256 слов идут в
// буфер строки, вывод - со слова горизонтального скролла (тема 5556). Точки
// в слове - с младшего бита. Ячейки палитры: 336-337 (1 бит), 256 + 4p
// (2 бита, p - палитра БК из 177662), 320 (4 бита), 0 (8 бит); со слоями -
// 328 + код из трех планов (1 бит), палитры 0/1/2 по слою (2 бита),
// 16 * слой (4 бита), общая (8 бит), 0 прозрачен, верхний слой первый.
//
// Сначала строка ложится в кадр R5G5B5 (по нему - поля crc и rgb для
// скриптов, и поток отрисовки перерисовывает из него поверхность, не трогая
// память AZ), потом на поверхность. Строка, у которой не менялись ни
// страницы памяти, ни регистры, ни палитра, не рисуется заново; раз в
// FULL_EVERY кадров рисуется все.
//
// Параметры:
//   video - устройство azbk-vga
class AZBKDisplay: public GenericDisplay
{
public:
    static const unsigned int WIDTH = 1024;
    static const unsigned int HEIGHT = 768;

private:
    AZBKVga * m_video = nullptr;

    std::vector<uint16_t> m_frame;          // R5G5B5, WIDTH x HEIGHT
    std::vector<uint32_t> m_native;         // R5G5B5 -> точка поверхности

    // Чем была нарисована строка: если то же, рисовать не нужно
    struct LineKey {
        uint32_t params[16];
        uint32_t pages[6];
        uint32_t gens[6];
        unsigned int count;
        bool valid;
    };
    std::vector<LineKey> m_keys;
    bool m_full_pass = true;
    unsigned int m_since_full = 0;
    uint64_t m_lines_drawn = 0;

    void make_key(unsigned int line, LineKey &key) const;
    void render_line15(unsigned int line, uint16_t * out) const;
    void put_line(unsigned int line);       // кадр -> поверхность, замок уже взят

protected:
    void render_all(bool force_render) override;

public:
    AZBKDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void state_restored() override;
    void set_renderer(VideoRenderer &vr) override;
    bool own_frames() const override { return true; }
    bool native_frame_rate(uint64_t &num, uint64_t &den) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    // От развертки
    void draw_line(unsigned int line);
    void frame_done();
    void redraw_all() { m_full_pass = true; }

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk_display(InterfaceManager *im, EmulatorConfigDevice *cd);
