#include <QPainter>
#include <QFontDatabase>
#include <QMouseEvent>

#include "dialogs/dialogs.h"
#include "emulator/utils.h"

#include "dumparea.h"
#include "hexeditorline.h"

#define LEFT_PADDING 2

DumpArea::DumpArea(QWidget *parent)
    : DOSFrame{parent},
    d(nullptr),
    start_address(0),
    hilight_address(_FFFF)
{
//    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(*font);
    char_width = fm.horizontalAdvance('0');
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
    this->start_address = start_address;
}

void DumpArea::go_to(unsigned int address)
{
    start_address = address;
    update();
}

void DumpArea::paintEvent([[maybe_unused]] QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    QPainter painter(this);

    painter.setFont(*font);

    if (d != nullptr)
    {
        unsigned int lines_count = size().height() / font_height - 1 - (frame_top?1:0);

        for (unsigned int i=0; i < lines_count; i++)
        {
            unsigned int address = start_address + i*16;
            if (address < d->get_size())
            {
                unsigned int x = char_width * LEFT_PADDING;
                unsigned int y = font_height * (i+1+(frame_top?1:0)) - 4;
                QString address_str = QString("%1: ").arg(address, 4, 16, QChar('0')).toUpper();
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
                        if (address == hilight_address)
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
    show_editor(start_address + mouse_to_offset(event->position().x(), event->position().y()));
}

void DumpArea::mousePressEvent(QMouseEvent *event)
{
    hilight_address = start_address + mouse_to_offset(event->position().x(), event->position().y());
    editor->hide();
    update();
}


void DumpArea::editor_return_pressed()
{
    editor->hide();
    QString str_value = editor->text();
    uint32_t value = parse_numeric_value('$'+str_value);
    d->set_value(editor_address, value);
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
