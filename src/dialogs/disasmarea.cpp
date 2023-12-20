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
    //qDebug() << font_height;
    font_height = fm.height();
    //qDebug() << font_height;
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
uint16_t DisAsmArea::area_crc16()
{
    uint16_t CRC = 0;
    for (unsigned int a = address_first; a < address_last + lines[lines_count-1].len; a++)
    {
        uint8_t b = cpu->read_mem(a);
        CRC16_update(&CRC, &b, 1);
    }

    return CRC;
}

void DisAsmArea::update_data()
{
    data_valid = true;
    screen_size = size().height() / font_height - 2;
    if (CRC != 0)
    {
        uint16_t CRC2 = area_crc16();
        if (CRC2 != CRC) CRC = 0;
    }

    if ( (address >= address_first) && (address <= address_last) && (CRC != 0))
    {
        //adddres is inside of the buffer
        unsigned int screen_first = lines[first_line].address;
        unsigned int screen_last = lines[first_line+screen_size-1].address;
        if ( (address >= screen_first) && (address <= screen_last) )
        {
            //adddres is inside of both the buffer and the screen
            for (unsigned int i = first_line; i < first_line+screen_size; i++)
                if (lines[i].address == address){
                    cursor_line = i-first_line;
                    update();
                    return;
                }
        } else {
            //adddres is inside of the buffer but ouside of the screen
        }
    } else {
        //adddres is outside of the buffer or CRC has changed
        lines_count = 0;

        // unsigned int a = address;
        // uint8_t buffer[15];
        // CRC = 0;

        // for (unsigned int i = 0; i < screen_size; i++)
        // {
        //     for (unsigned int j = 0; j < disasm->max_command_length; j++)
        //         buffer[j] = cpu->read_mem(a+j);
        //     QString s;
        //     unsigned int c = disasm->disassemle(&buffer, a, disasm->max_command_length, &s);
        //     lines[i].address = a;
        //     lines[i].command = s;
        //     lines[i].current = (a == cpu->get_pc());
        //     lines[i].breakpoint = false;
        //     lines[i].len = c;
        //     lines[i].code = bytes_dump(&buffer, c);
        //     a += c;

        //     CRC16_update(&CRC, buffer, c);
        // }

        disassemble_lines(0, screen_size, address);
        CRC = area_crc16();
        first_line = 0;
        cursor_line = 0;
    }
}

void DisAsmArea::disassemble_lines(int index, int count, unsigned int address)
{
    //index - buffer index to start from
    //lines - expected lines to disassemble
    //address - address to begin in case of full update
    unsigned int a;
    uint8_t buffer[15];

    unsigned int pc = cpu->get_pc();

    if (index == 0) {
        lines_count = 0;
        a = address;
    } else
        a = lines[lines_count-1].address + lines[lines_count-1].len;

    for (unsigned int i = index; i < index+count; i++)
    {
        for (unsigned int j = 0; j < disasm->max_command_length; j++)
            buffer[j] = cpu->read_mem(a+j);
        QString s;
        unsigned int c = disasm->disassemle(&buffer, a, disasm->max_command_length, &s);
        lines[i].address = a;
        lines[i].command = s;
        lines[i].current = (a == pc);
        lines[i].breakpoint = false;
        lines[i].len = c;
        lines[i].code = bytes_dump(&buffer, c);
        a += c;
    }

    lines_count += count;
    address_first = lines[0].address;
    address_last = lines[lines_count-1].address;
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

    //qDebug() << "Heigth: " << size().height();

    if (!data_valid) update_data();

    QPainter painter(this);

    //painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));

    //unsigned int lines_count = screen_size;

    for (unsigned int i=0; i < screen_size; i++)
    {
        unsigned int line = first_line + i;
        if (line < lines_count)
        {
            unsigned int x = char_width*2;
            unsigned int y = font_height * (i+2);
            if (i == cursor_line)
            {
                painter.setPen(SELECTION);
                painter.fillRect(char_width, y-font_height+3, (size().width() / char_width - 2)*char_width, font_height, SELECTION_BACK);
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

void DisAsmArea::keyPressEvent(QKeyEvent *event)
{
    //qDebug() << "Disasm" << event->key();

    switch (event->key()) {
    case Qt::Key_F4:
    case Qt::Key_F7:
    case Qt::Key_F8:
    case Qt::Key_F9:
        emit command_key(event);
        break;
    case Qt::Key_Down:
        move_cursor(1);
        break;
    case Qt::Key_PageDown:
        move_cursor(screen_size);
        break;
    case Qt::Key_Up:
        move_cursor(-1);
        break;
    case Qt::Key_PageUp:
        move_cursor(-screen_size);
        break;
    default:
        DOSFrame::keyPressEvent(event);
        break;
    }
}

void DisAsmArea::move_cursor(int increment)
{
    int new_cursor = cursor_line + increment;
    if (new_cursor >= 0 && new_cursor < screen_size)
    {
        //Cursor is still inside the screen, so just move it
        cursor_line += increment;
    } else
    if (new_cursor >= screen_size) {
        //Cursor is going below the screen
        //We have to shift all the screen down and keep cursor position
        if (first_line + screen_size + increment >= lines_count)
        {
            //We need to extend our buffer
            int lines_to_add = first_line + screen_size + increment - lines_count;
            disassemble_lines(lines_count, lines_to_add);
            CRC = area_crc16();
        }
        first_line += increment;
    } else
    if (new_cursor < 0) {
        if (first_line + increment >= 0)
        {
            first_line += increment;
        } else {
            //We can't disassemble backwards, so just reset the screen to the start
            first_line = 0;
            //QApplication::beep();
        }
    }
    update();
}
