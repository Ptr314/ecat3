#include <QMouseEvent>
#include <QPainter>

#include "emulator/utils.h"
#include "dialogs/dialogs.h"
#include "disasmarea.h"
#include "libs/crc16.h"

DisAsmArea::DisAsmArea(QWidget *parent)
    : DOSFrame{parent},
    lines_count(0),
    address_first(_FFFF),
    address_last(_FFFF),
    max_lines(0),
    data_valid(false),
    CRC(0)
{
    //QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(*font);
    char_width = fm.horizontalAdvance('0');
    qDebug() << font_height;
    font_height = fm.height();
    qDebug() << font_height;
}

void DisAsmArea::set_data(Emulator * e, CPU * cpu, DisAsm * disasm, unsigned int address)
{
    this->e = e;
    this->cpu = cpu;
    this->disasm = disasm;
    lines_count = 0;                                    //Disassembled buffer size
    first_line = 0;                                     //First buffer line on screen
    address_first = _FFFF;                              //Address at first buffer line
    address_last = _FFFF;                               //Address at last buffer line
    cursor_line = 0;                                    //Cursor position
    go_to(address);
}

void DisAsmArea::invalidate()
{
    address_first = _FFFF;                              //Address at first buffer line
    address_last = _FFFF;                               //Address at last buffer line
    data_valid = false;
    update();
}

void DisAsmArea::go_to(unsigned int address)
{
    this->address = address;
    data_valid = false;
    update();
}

unsigned int DisAsmArea::get_address_at_cursor()
{
    return lines[cursor_line + first_line].address;
}

void DisAsmArea::update_data()
{
    data_valid = true;
    screen_size = size().height() / font_height - 1;
    if (CRC != 0)
    {
        uint16_t CRC2 = 0;
        for (unsigned int a = address_first; a < address_last + lines[lines_count-1].len; a++)
        {
            uint8_t b = cpu->read_mem(a);
            CRC16_update(&CRC2, &b, 1);
        }

        if (CRC2 != CRC) CRC = 0;
    }

    if ( (address >= address_first) && (address <= address_last) && (CRC != 0))
    {
        unsigned int screen_first = lines[first_line].address;
        unsigned int screen_last = lines[first_line+screen_size-1].address;
        if ( (address >= screen_first) && (address <= screen_last) )
        {
            for (unsigned int i = first_line; i < first_line+screen_size; i++)
                if (lines[i].address == address){
                    cursor_line = i-first_line;
                    update();
                    return;
                }
        }
    } else {
        lines_count = 0;

        unsigned int a = address;
        uint8_t buffer[15];
        CRC = 0;

        for (unsigned int i = 0; i < screen_size; i++)
        {
            for (unsigned int j = 0; j < disasm->max_command_length; j++)
                buffer[j] = cpu->read_mem(a+j);
            QString s;
            unsigned int c = disasm->disassemle(&buffer, cpu->get_pc(), disasm->max_command_length, &s);
            lines[i].address = a;
            lines[i].command = s;
            lines[i].current = (a == cpu->get_pc());
            lines[i].breakpoint = false;
            lines[i].len = c;
            lines[i].code = bytes_dump(&buffer, c);
            a += c;

            CRC16_update(&CRC, buffer, c);
        }
        lines_count = screen_size;
        address_first = lines[0].address;
        address_last = lines[lines_count-1].address;
        first_line = 0;
        cursor_line = 0;
    }
}
void DisAsmArea::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint point = event->pos();
        cursor_line = point.y() / font_height - 1;
        update();
    }
}

void DisAsmArea::paintEvent(QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    if (!data_valid) update_data();

    QPainter painter(this);

    //painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));

    unsigned int lines_count = size().height() / font_height - 2;

    for (unsigned int i=0; i < lines_count; i++)
    {
        unsigned int line = first_line + i;
        if (line < lines_count)
        {
            unsigned int x = 20;
            unsigned int y = font_height * (i+2);
            if (i == cursor_line)
            {
                painter.setPen(SELECTION);
                painter.fillRect(char_width, y-font_height+3, floor(static_cast<float>(size().width()) / char_width - 2)*char_width, font_height, SELECTION_BACK);
            } else {
                painter.setPen(QColor(0,0,0));
            }
            QString prefix_str = (lines[line].address == cpu->get_pc())?"►":" ";
            painter.drawText(x, y, prefix_str);
            x += char_width * (prefix_str.length()+1);


            QString address_str = QString("%1").arg(lines[line].address, 4, 16, QChar('0')).toUpper() + " :";
            painter.drawText(x, y, address_str);
            x += char_width * (6+1);

            QString postfix_str = (cpu->check_breakpoint(lines[line].address))?"@":" ";
            painter.drawText(x, y, postfix_str);
            x += char_width * (postfix_str.length()+1);

            painter.drawText(x, y, lines[line].code);
            x += char_width * (3*disasm->max_command_length+4);

            painter.drawText(x, y, lines[line].command);
        }
    }
}
