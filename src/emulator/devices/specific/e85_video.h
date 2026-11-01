// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Видеоконтроллер НВ1 и расширитель НВ2 Электроники МС 0585

#pragma once

#include <vector>

#include "emulator/core.h"

class E85Display;

// Три страницы по 32 КБ: «синяя» на НВ1 и «красная», «зелёная» на НВ2. Рисует
// программа: прямо в страницу через окно в адресном пространстве либо вектором
// (ТО3, ТО6). Устройство отвечает в трёх диапазонах карты:
//   регистры НВ1 - адреса 000-177 позиции;
//   идентификация НВ2 - позиция расширителя, с базой 400;
//   окно памяти - routed-диапазон с базой, равной его началу.
//
// Что ТО не договаривает, взято из теста в ПЗУ автодиагностики (137130-142444):
//   - линия А (вектор 320) - конец кадра, линия Б (324) - готовность вектора;
//     в ТО наоборот;
//   - сдвиг РСИ действует и на экран, и на обращения ЦП и векторы: координаты
//     программы всегда совпадают с экранными, а содержимое памяти уезжает;
//   - РКС<0> = 1 - 625 строк и 50 Гц, 0 - 525 и 60 Гц.
// Разряды слова идут слева направо от младшего (рис. 2 ТО3 и Xhomer).
class E85Video: public AddressableDevice
{
public:
    enum { WORDS = 16384, LINE_WORDS = 64, LINES = 256 };

private:
    Interface i_irqa;               // конец кадра, импульс
    Interface i_irqb;               // готовность вектора при разрешении, уровень
    Interface i_init;

    std::vector<uint16_t> m_planes[3];  // 0 - синяя, 1 - зелёная, 2 - красная

    bool m_expansion = false;

    unsigned int m_csr = 0;
    unsigned int m_rud1 = 0;
    unsigned int m_rud2 = 0;
    unsigned int m_ruc[8] = {};
    unsigned int m_scroll = 0;
    unsigned int m_x = 0;
    unsigned int m_y = 0;
    unsigned int m_pattern = 0;
    unsigned int m_base = 0;        // разряды 21-15 адреса окна

    unsigned int m_busy = 0;        // тактов до готовности вектора
    bool m_vblank = false;
    bool m_even = false;

    unsigned int m_frames = 0;
    unsigned int m_vectors = 0;

    MemoryMapper * m_mapper = nullptr;
    E85Display * m_display = nullptr;

    void clear_registers();
    void update_irqb();
    unsigned int plane_enables() const;
    unsigned int memory_index(unsigned int address) const;
    unsigned int read_register(unsigned int offset) const;
    void write_register(unsigned int offset, unsigned int value);
    void run_vector(unsigned int count);
    void bit_vector(unsigned int plane, unsigned int op, unsigned int count);
    void word_vector(unsigned int plane, unsigned int op, unsigned int count);

public:
    E85Video(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;

    unsigned int route(unsigned int address, unsigned int mode) override;
    unsigned int get_value(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    // Для развёртки
    void attach_display(E85Display * display) { m_display = display; }
    const uint16_t * plane(unsigned int n) const { return m_planes[n].data(); }
    bool expansion() const { return m_expansion; }
    bool pal() const { return (m_csr & 1) != 0; }
    bool color_map() const { return (m_csr & 02000) != 0; }
    unsigned int scroll() const { return m_scroll; }
    unsigned int rud1() const { return m_rud1; }
    unsigned int rud2() const { return m_rud2; }
    unsigned int ruc(unsigned int index) const { return m_ruc[index]; }
    void frame_end();
    void frame_start();
};

// Развёртка: рисует строки на поверхности и сообщает контроллеру о конце кадра
class E85Display: public GenericDisplay
{
private:
    E85Video * m_video = nullptr;

    unsigned int m_pos = 0;         // такты внутри строки
    unsigned int m_line = 0;
    unsigned int m_line_clocks = 427;

    // Строка рисуется заново, только если в неё писали или сменился режим.
    // Физическая строка памяти, не экрана
    bool m_dirty[E85Video::LINES];
    bool m_redraw = true;           // перерисовать весь следующий кадр
    bool m_full_pass = true;        // этот кадр рисуется целиком
    unsigned int m_since_full = 0;

    std::vector<uint32_t> m_rgba;

    void update_timing();
    void line_done();
    void render_line(unsigned int line);
    void render_line_unlocked(unsigned int line);

protected:
    void render_all(bool force_render) override;

public:
    E85Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void state_restored() override;
    void set_renderer(VideoRenderer &vr) override;
    void clock(unsigned int counter) override;
    bool own_frames() const override { return true; }
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    void line_changed(unsigned int physical_line) { m_dirty[physical_line & (E85Video::LINES - 1)] = true; }
    void all_changed() { m_redraw = true; }
};

ComputerDevice * create_e85_video(InterfaceManager *im, EmulatorConfigDevice *cd);
ComputerDevice * create_e85_display(InterfaceManager *im, EmulatorConfigDevice *cd);
