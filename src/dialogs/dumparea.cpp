// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DOS-like hex dump widget's source

#include <QPainter>
#include <QFontDatabase>
#include <QMouseEvent>
#include <cmath>

#include "dialogs/dialogs.h"
#include "dialogs/numfmt.h"
#include "emulator/utils.h"

#include "dumparea.h"
#include "hexeditorline.h"

#define LEFT_PADDING 2

DumpArea::DumpArea(QWidget *parent)
    : DOSFrame{parent},
    d(nullptr),
    global_offset(0),
    start_address(0),
    hilight_address(_FFFF),
    lines_count(0),
    data_size(0),
    current_buffer(0),
    buffer_is_valid(false),
    m_base(16),
    m_word(false)
{
//    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(*font);

#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    char_width = fm.horizontalAdvance('0');
#endif

    font_height = fm.height();

    editor = new HexEditorLine(this, *font, char_width, font_height);
    connect(editor, &HexEditorLine::esc_pressed, this, &DumpArea::editor_escape_pressed);
    connect(editor, &HexEditorLine::return_pressed, this, &DumpArea::editor_return_pressed);
    connect(editor, &HexEditorLine::tab_pressed, this, &DumpArea::editor_tab_pressed);
    editor->hide();
}

void DumpArea::set_data(Emulator * e, AddressableDevice * d, unsigned int start_address)
{
    this->d = d;
    this->e = e;
    data_size = d->get_size();
    scroll_size = data_size;
    go_to(start_address);
}

void DumpArea::set_view(int base, bool word)
{
    m_base = (base == 8) ? 8 : 16;
    m_word = word;
    editor->hide();
    editor->set_format(m_base, cell_digits(m_base, m_word));
    if (d != nullptr) go_to(global_offset, start_address);
}

//Wide enough for the last address of the device, as it is shown
int DumpArea::address_digits() const
{
    const unsigned int last = (data_size > 0) ? data_size - 1 : 0;
    return addr_digits(last + global_offset, m_base);
}

//A byte, or the little-endian word from the address: every processor here
//keeps the low byte first
unsigned int DumpArea::cell_value(unsigned int address)
{
    unsigned int v = d->get_direct(address) & 0xFF;
    if (m_word && address + 1 < data_size)
        v |= (d->get_direct(address + 1) & 0xFF) << 8;
    return v;
}

void DumpArea::go_to(int offset, int address)
{
    //A word starts on an even address
    if (m_word) address &= ~1;
    global_offset = offset;
    start_address = address;
    buffer_is_valid = false;

    if (lines_count > 0) {
        //Both unsigned: the old "< 0" test was never true, and a device smaller
        //than the window (a 4-byte port) wrapped the scroll range to 4 GB
        scroll_size = (data_size > lines_count*16) ? (data_size - lines_count*16) : data_size;
    }
    set_scroll(scroll_size, address);
    update();
}

void DumpArea::go_to(int address)
{
    int new_address = address;
    if (address < 0) {
        new_address = d->get_size() - lines_count*16;
        if (new_address < 0) new_address = 0;
    }
    go_to(global_offset, new_address);
}

void DumpArea::paintEvent(MAYBE_UNUSED QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    QPainter painter(this);

    painter.setFont(*font);

    if (d != nullptr)
    {
        //As many lines as the window holds, but no more than the two fixed
        //buffers below have room for: a dialog stretched on a tall monitor
        //went past 64 lines and wrote beyond them. Nor fewer than none - the
        //count is unsigned, and a window shorter than two lines wrapped around
        const int fit = size().height() / font_height - 1 - (frame_top?1:0);
        const int room = static_cast<int>(sizeof(buffer[0]) / 16);
        lines_count = static_cast<unsigned int>(fit < 0 ? 0 : (fit > room ? room : fit));

        if (!buffer_is_valid) fill_buffer(true);

        //16 bytes a line, as 16 bytes or 8 words; the characters follow the cells
        const unsigned int u = unit();
        const int digits = cell_digits(m_base, m_word);
        const int adigits = address_digits();
        const unsigned int chars_x = (16/u) * (digits+1) + 2;
        for (unsigned int i=0; i < lines_count; i++)
        {
            unsigned int address = start_address + i*16;
            if (address < d->get_size())
            {
                unsigned int x = char_width * LEFT_PADDING;
                unsigned int y = font_height * (i+1+(frame_top?1:0)) - 4;
                QString address_str = fmt_num(address + global_offset, m_base, adigits) + ": ";
                painter.setPen(TEXT_COLOR);
                painter.drawText(x, y, address_str);
                x += address_str.length() * char_width;
                for (unsigned int j=0; j<16; j+=u)
                {
                    address = start_address + i*16 + j;
                    if (address < d->get_size())
                    {
                        unsigned int value = cell_value(address);
                        bool changed = false;
                        for (unsigned int k=0; k<u && j+k<16 && address+k < data_size; k++)
                            if (((value >> (8*k)) & 0xFF) != buffer[current_buffer][i*16 + j + k]) changed = true;
                        if ((hilight_address >= address && hilight_address < address + u) || changed)
                        {
                            painter.setPen(TEXT_HILIGHT);
                        } else {
                            painter.setPen(TEXT_COLOR);
                        }
                        painter.drawText(x + (j/u)*(digits+1)*char_width, y, fmt_num(value, m_base, digits));
                        for (unsigned int k=0; k<u && address+k < data_size; k++)
                        {
                            QString c = QString::fromStdString(e->translate_char((value >> (8*k)) & 0xFF));
                            painter.drawText(x + char_width*(chars_x + j + k), y, c);
                        }
                    }
                }
            }
        }
    }
}

void DumpArea::show_editor(unsigned int address)
{
    //A word is edited from its even address
    if (m_word) address &= ~1u;
    if (address >= data_size) return;
    editor_address = address;
    const int digits = cell_digits(m_base, m_word);
    int yc = (editor_address - start_address) / 16;
    int xb = ((editor_address - start_address) % 16) / unit();
    editor->move((xb*(digits+1) + address_digits() + 2 + LEFT_PADDING)*char_width - char_width/2, (yc+(frame_top?1:0))*font_height+3-4);

    editor->setText(fmt_num(cell_value(editor_address), m_base, digits));
    editor->show();
    editor->selectAll();
    editor->setFocus();
}

unsigned int DumpArea::mouse_to_offset(unsigned int  x, unsigned int y)
{
    const int digits = cell_digits(m_base, m_word);
    const int addr = address_digits() + 2;
    const int cells = 16 / unit();
    unsigned int border = char_width * (LEFT_PADDING + addr + cells*(digits+1) + 2);
    int yc = (int)(y/font_height) - (frame_top?1:0);
    if (yc < 0) yc = 0;
    if (x<border)
    {
        int xc = ((int)x - (int)(char_width * LEFT_PADDING)) / (int)char_width;
        int xb = (xc - addr) / (digits+1);

        if (xb < 0) xb = 0;
        if (xb > cells-1) xb = cells-1;

        return yc*16 + xb*unit();
    } else {
        int xc = (x-border)/char_width;
        if (xc > 15) xc = 15;
        if (m_word) xc &= ~1;
        return yc*16 + xc;
    }
}

void DumpArea::mouseDoubleClickEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    show_editor(start_address + mouse_to_offset(event->position().x(), event->position().y()));
#else
    show_editor(start_address + mouse_to_offset(event->x(), event->y()));
#endif

}

void DumpArea::mousePressEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    hilight_address = start_address + mouse_to_offset(event->position().x(), event->position().y());
#else
    hilight_address = start_address + mouse_to_offset(event->x(), event->y());
#endif
    editor->hide();
    update();
}


void DumpArea::editor_return_pressed()
{
    editor->hide();
    QString str_value = editor->text();
    if (str_value.isEmpty()) return;
    uint32_t value;
    try {
        value = parse_input(str_value, m_base);
    } catch (std::exception &) {
        return;
    }
    //A word is one word write: a register that acts on a write (the DW data
    //register, a TBUF) must see one access, not two byte halves
    e->invoke([&]() {
        if (m_word && (editor_address & 1) == 0 && editor_address + 1 < data_size)
            d->set_value_word(editor_address, value & 0xFFFF, true);
        else
            d->set_value(editor_address, value & 0xFF, true);
    });
    update();
}

void DumpArea::editor_tab_pressed()
{
    //TODO: HEX editor: Do something if the next address is below the last line
    editor_return_pressed();
    show_editor(editor_address + unit());
}

void DumpArea::editor_escape_pressed()
{
    editor->hide();
}

void DumpArea::page_down()
{
    if (lines_count > 0) {
        //Compared as a sum: data_size - page wrapped round on a device smaller
        //than a page (a port), and the last page was never reached
        const unsigned int page = lines_count*16;
        if (start_address + 2*page > data_size)
            go_to(-1);
        else
            go_to(global_offset, start_address + page);
    }
}

void DumpArea::page_up()
{
    if (lines_count > 0) {
        int new_address = start_address - lines_count*16;
        if (new_address < 0) new_address = 0;
        go_to(global_offset, new_address);
    }
}

void DumpArea::fill_buffer(bool prefill)
{
    unsigned int fill_buffer = current_buffer ^ 1;
    for (unsigned int i=0; i < lines_count; i++)
        for (unsigned int j=0; j<16; j++)
        {
            unsigned int address = start_address + i*16 + j;
            uint8_t data = d->get_direct(address);
            if (address < d->get_size()) {
                buffer[fill_buffer][i*16 + j] = data;
                if (prefill) buffer[current_buffer][i*16 + j] = data;
            }
        }
    buffer_is_valid = true;
}

void DumpArea::update_view()
{
    current_buffer ^= 1;
    fill_buffer();
    hilight_address = _FFFF;
    update();
}

void DumpArea::reset_buffer()
{
    buffer_is_valid = false;
}
