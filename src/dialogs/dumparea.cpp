#include <QPainter>
#include <QFontDatabase>
#include <QMouseEvent>
#include <cmath>

#include "dialogs/dialogs.h"
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
    buffer_is_valid(false),
    current_buffer(0)
{
//    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(*font);

#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    char_width = fm.horizontalAdvance('0');
#endif

    font_height = fm.height();

    editor = new HexEditorLine(this, *font, char_width, font_height);
    connect(editor, SIGNAL(esc_pressed()), this, SLOT(editor_escape_pressed()));
    connect(editor, SIGNAL(return_pressed()), this, SLOT(editor_return_pressed()));
    connect(editor, SIGNAL(tab_pressed()), this, SLOT(editor_tab_pressed()));
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

void DumpArea::go_to(int offset, int address)
{
    global_offset = offset;
    start_address = address;
    buffer_is_valid = false;

    if (lines_count > 0) {
        scroll_size = data_size - lines_count*16;
        if (scroll_size < 0) scroll_size = data_size;
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

void DumpArea::paintEvent([[maybe_unused]] QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    QPainter painter(this);

    painter.setFont(*font);

    if (d != nullptr)
    {
        lines_count = size().height() / font_height - 1 - (frame_top?1:0);

        if (!buffer_is_valid) fill_buffer(true);

        for (unsigned int i=0; i < lines_count; i++)
        {
            unsigned int address = start_address + i*16;
            if (address < d->get_size())
            {
                unsigned int x = char_width * LEFT_PADDING;
                unsigned int y = font_height * (i+1+(frame_top?1:0)) - 4;
                QString address_str = QString("%1: ").arg(address + global_offset, 4, 16, QChar('0')).toUpper();
                painter.setPen(TEXT_COLOR);
                painter.drawText(x, y, address_str);
                x += address_str.length() * char_width;
                for (unsigned int j=0; j<16; j++)
                {
                    address = start_address + i*16 + j;
                    if (address < d->get_size())
                    {
                        uint8_t data = d->get_value(address);
                        QString data_str = QString("%1 ").arg(data, 2, 16, QChar('0')).toUpper();
                        if (address == hilight_address || data != buffer[current_buffer][i*16 + j])
                        {
                            painter.setPen(TEXT_HILIGHT);
                        } else {
                            painter.setPen(TEXT_COLOR);
                        }
                        painter.drawText(x + j*data_str.length()*char_width, y, data_str);
                        QString c = QString(*e->translate_char(data));
                        painter.drawText(x + char_width*(16 * data_str.length() + 2 + j), y, c);
                    }
                }
            }
        }
    }
}

void DumpArea::show_editor(unsigned int address)
{
    //qDebug() << Qt::hex << address;
    editor_address = address;
    int yc = (editor_address - start_address) / 16;
    int xb = (editor_address - start_address) % 16;
    editor->move((xb*3 + 6 + LEFT_PADDING)*char_width - char_width/2, (yc+(frame_top?1:0))*font_height+3-4);

    uint8_t data = d->get_value(editor_address);
    QString data_str = QString("%1").arg(data, 2, 16, QChar('0')).toUpper();
    editor->setText(data_str);
    editor->show();
    editor->selectAll();
    editor->setFocus();
}

unsigned int DumpArea::mouse_to_offset(unsigned int  x, unsigned int y)
{
    int border = char_width * (LEFT_PADDING + 6 /* addr */ + 16*3 /* data */ + 2);
    if (x<border)
    {
        int xc = floor((x-floor(char_width * LEFT_PADDING))/char_width);
        int yc = floor(y/font_height - (frame_top?1:0));
        int xb = (xc - 6) / 3;

        if (yc < 0) yc = 0;
        if (xb < 0) xb = 0;
        if (xb > 15) xb = 15;

        return yc*16 + xb;
    } else {
        int xc = (x-border)/char_width;
        int yc = y/font_height - (frame_top?1:0);
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
    uint32_t value = parse_numeric_value('$'+str_value);
    d->set_value(editor_address, value, true);
    update();
}

void DumpArea::editor_tab_pressed()
{
    //TODO: HEX editor: Do something if the next address is below the last line
    editor_return_pressed();
    show_editor(editor_address + 1);
}

void DumpArea::editor_escape_pressed()
{
    editor->hide();
}

void DumpArea::page_down()
{
    if (lines_count > 0) {
        int new_address = start_address + lines_count*16;
        if (new_address > data_size - lines_count*16)
            go_to(-1);
        else
            go_to(global_offset, new_address);
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
            uint8_t data = d->get_value(address);
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
