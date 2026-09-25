// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common i8275(КР580ВГ75)-based display controller device

#pragma once

#include "emulator/core.h"
#include "emulator/devices/common/i8275.h"

class I8257;

//The tallest screen the 8275 can be programmed for is 64 character rows
#define I8275D_MAX_ROWS 64
//Высота знакоместа у ВГ75 задается четырьмя разрядами, то есть не больше 16
#define I8275D_MAX_H    16

class I8275Display: public GenericDisplay, public I8275RowSink
{
private:
    //One character row as the controller delivered it. Besides the bytes DMA
    //fetched, it carries everything outside them the row's appearance depends
    //on, sampled at the moment the beam reached the row: the font bank on the
    //~high line (the Apogey drives it from the CPU's INTE output and can change
    //it inside a frame), the cursor and the two blink phases. The pixels are
    //then drawn on the render thread, which is the only thread allowed to touch
    //the surface, and the picture still comes out as it looked row by row
    struct RowSnapshot {
        uint8_t         data[I8275_MAX_ROW];
        unsigned int    len;
        unsigned int    font_bank;
        unsigned int    palette_base;       //Старшие разряды адреса палитры
        unsigned int    cursor_col;
        unsigned int    cursor_row;
        unsigned int    cursor_mode;        //RegMode[3] & 0x30
        unsigned int    add_mode;           //Font line shift, RegMode[3] >> 7
        bool            transparent;        //Field attributes take no position
        bool            blink;              //Cursor blink phase
        bool            blink_char;         //Character blink phase
    };

    RAM * Memory;
    //Знакогенератор - не обязательно ПЗУ: у Арго это ОЗУ, которое машина
    //подставляет себе в адресное пространство и заполняет сама
    AddressableDevice * Font;
    I8275 * VG75;
    I8257 * DMA;
    unsigned int Channel;
    Interface i_high;             //Font select
    //Палитра адресуется не только атрибутом: у Юниора её старшие разряды -
    //те же линии, что выбирают банк знакогенератора (~high) и страницу
    //цветов (~palette_page), так что одно и то же сочетание атрибутных бит
    //даёт в тексте и в графике разные цвета
    Interface i_palette_page;
    //Режим ZX Spectrum у Арго: разряд 4 регистра конфигурации. Как устроен
    //режим, разобрано по ZX.COM и по работающему ПЗУ Спектрума, см.
    //draw_row_zx()
    Interface i_zx;
    //Цвет бордюра. Рисовать его здесь пока нечем - его не рисует ни одна
    //машина проекта, - но линия заведена и видна сценарию: во время загрузки
    //с ленты ПЗУ Спектрума гоняет по бордюру полосы, и это единственный
    //признак, что лента читается
    Interface i_border;
    unsigned int m_border = 0;
    uint64_t m_border_changes = 0;
    //Цвет бордюра на каждую растровую строку кадра. Заполняется на потоке
    //эмуляции по ходу развертки, читается при отрисовке на своем потоке -
    //без замка, ровно как снимки строк рядом: худшее, что может выйти, это
    //граница полосы, уехавшая на кадр.
    //
    //Шаг постоянный нарочно. Индекс считается на одном потоке, а читается на
    //другом, и привязывать его к геометрии кадра, которая между ними может
    //разойтись, незачем
    uint8_t m_border_line[I8275D_MAX_ROWS * I8275D_MAX_H];
    bool AttrDelay;
    unsigned int RGB[3];
    bool RGBInv;
    //The eight colours in the pixel format of the renderer
    uint32_t rgba_colors[8];
    //Шестнадцать цветов Спектрума в формате отрисовщика: три разряда цвета
    //плюс яркость, ровно как RGBI у самого Арго
    uint32_t zx_colors[16];
    unsigned int m_zx_bitmap = 0;   //Где лежит битовая карта экрана Спектрума
    unsigned int m_zx_attr = 0;     //Где лежат его атрибуты
    bool m_frame_zx = false;        //Снят вместе с геометрией кадра

    //Таблица цветов (ПЗУ палитры). Адрес: точка знакогенератора в разряде 0,
    //атрибутные биты выше, банк и страница - в двух старших. Ответ - пара
    //"цвет символа/цвет фона" в соседних ячейках
    ROM * Palette;

    //Field attribute state. It carries from row to row down the screen, so it
    //lives here and is reset once per frame rather than once per row
    bool FAReverse, FAUnder, FABlink;
    unsigned int FAColor;
    unsigned int FAColorBg;
    uint8_t NextAttr;

    RowSnapshot m_rows[I8275D_MAX_ROWS];
    bool m_have_frame;

    //The geometry the finished frame was fetched with. The controller runs on
    //the emulation thread and may be reprogrammed at any moment, so the render
    //thread must never read its registers while laying a frame out - it would
    //draw one row with the width of one raster and the height of another
    unsigned int m_frame_cpl;
    unsigned int m_frame_lps;
    unsigned int m_frame_h;

    //Height of the surface set_renderer() was handed. The renderer is resized
    //on the interface thread, and drawing must stop at the buffer it built
    unsigned int m_surface_sy;

    //Разводка адреса знакогенератора. По умолчанию, как у Радио-86РК и
    //родни, это банк, код знака и номер строки развёртки подряд:
    //адрес = банк*1024 + код*8 + строка. Параметр font_address описывает
    //другую разводку строкой из 11 знаков, от старшего разряда адреса к
    //младшему: 0-7 - разряд кода знака, a/b/c/d - разряды счётчика строк,
    //B - линия выбора банка (~high), '-' - постоянный ноль. Разряды
    //независимы, поэтому адрес складывается из трёх заранее посчитанных
    //слагаемых, а не собирается по биту на каждую точку
    unsigned int m_font_code[256];
    unsigned int m_font_line[16];
    unsigned int m_font_high = 0;
    bool m_font_mapped = false;
    //Знакогенераторы Радио-86РК и родни хранят точки инвертированными, и
    //разрядка знака получается из нулей. У Арго знакогенератор лежит в ОЗУ
    //и пишется прямым - пустая ячейка там означает пустое знакоместо
    bool m_font_invert = true;
    //Начало знакогенератора в устройстве, названном font. У Арго он лежит
    //не отдельной микросхемой, а страницей 3 второй банки того же ОЗУ
    unsigned int m_font_base = 0;
    //В старшем банке знакогенератора (том, что выбирает ~high) точка вдвое
    //шире: у Юниора это графический режим, где знакоместо в 6 точек показывает
    //три псевдоточки - отсюда и 240 на строку при 80 знакоместах. Сами точки
    //лежат в трёх младших разрядах байта, а по высоте ПЗУ уже удвоено: на
    //знакоместо в четыре растровых строки приходятся две одинаковые пары
    bool m_font_high_wide = false;
    //На сколько знакомест левее показывать курсор, чем говорит команда
    //загрузки. Нужно машине, чья программа прибавляет к колонке единицу,
    //которой нечего компенсировать - см. Argo.md
    unsigned int m_cursor_left = 0;

    unsigned int font_address(unsigned int code, unsigned int line, unsigned int bank) const;
    void draw_row_zx(unsigned int Lin);
    unsigned int map_color(unsigned int v);
    void set_attr(uint8_t v, unsigned int palette_base);
    void reset_attr();
    void clear_rows();
    bool follow_resolution();
    void clear_surface();

public:
    I8275Display(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;
    emulator::Result load_config(SystemData *sd) override;
    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;
    void reset(bool cold) override;

    //--------------------- I8275RowSink, on the emulation thread -------------
    void interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value) override;
    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    void row_complete(unsigned int row, const uint8_t * data, unsigned int count) override;
    void raster_line(unsigned int row, unsigned int line) override;
    void frame_complete() override;
    void display_blanked() override;

protected:
    void render_all(bool force_render = false) override;
    virtual void draw_row(unsigned int row);
};

ComputerDevice * create_i8275display(InterfaceManager *im, EmulatorConfigDevice *cd);
