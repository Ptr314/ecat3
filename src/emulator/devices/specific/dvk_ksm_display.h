// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: КСМ (ДВК character display controller) video output

#pragma once

#include "emulator/core.h"

class DVKKGD;

// Изображение контроллера символьного монитора ДВК. Сам КСМ - отдельная
// машина на КР580ВМ80А со своим ПЗУ, она живёт в конфигурации; здесь только
// развёртка его видеопамяти (16 КБ, C000-FFFF его процессора).
//
// Растр 800 x 275: 25 строк по 11 линий, знакоместо 10 x 11, знак 7 x 8 из
// знакогенератора (ksm_03). Верхняя строка - служебная, её 80 знаков лежат
// с F8B0. Остальные 24 - кольцо из 48 строк по 128 байт с E030, первую из
// них выбирает порт A ВВ55 (~line). Три нижние линии знакоместа берут знак
// из той же позиции на 8 КБ ниже - там курсор, он мигает; управляющие коды
// (меньше 040) показываются мигающими. Так это разобрано в MAME (dvk_ksm.cpp).
//
// Кадр рисуется целиком потоком отрисовки, 50 раз в секунду времени машины.
//
// К тому же монитору подключается графический контроллер КГД (параметр kgd):
// его картинка 400 x 286 с удвоенными точками ложится поверх текста. Своей
// развёртки у КГД нет (схема ПБА4.135.998, XP2): кадр и строки он берёт от
// КСМ и по кадровому синхроимпульсу начинает видеопамять с нуля. Кадр КСМ -
// 28 строк знаков (строки 4-31 его ПЗУ D16): две на гашение и кадровый
// импульс, строка из F830 (пробелы), служебная и 24 строки текста. Видимые 26
// строк - это и есть 286 линий КГД, поэтому текст стоит на 11 линий ниже края
// картинки, а над служебной строкой рисуется строка из F830.
class KSMDisplay: public GenericDisplay
{
private:
    Interface i_line;

    Memory * m_vram = nullptr;
    Memory * m_font_rom = nullptr;
    DVKKGD * m_kgd = nullptr;
    unsigned int m_text_top = 0;        // первая линия текста в растре
    std::vector<uint8_t> m_font;
    uint32_t m_colors[2] = {0, 0};
    uint8_t m_palette[2][3] = {{0, 0, 0}, {0, 255, 0}};

    unsigned int m_frame_ticks = 0;     // тактов на кадр
    unsigned int m_ticks = 0;
    unsigned int m_frame = 0;

    void render_row(unsigned top, unsigned offset, bool blink);
    void render_graphics();

protected:
    void render_all(bool force_render) override;

public:
    KSMDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void set_renderer(VideoRenderer &vr) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
};

ComputerDevice * create_ksm_display(InterfaceManager *im, EmulatorConfigDevice *cd);
