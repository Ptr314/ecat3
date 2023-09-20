#include <QPainter>

#include "emulator/utils.h"
#include "dialogs/dialogs.h"
#include "disasmarea.h"

DisAsmArea::DisAsmArea(QWidget *parent)
    : QWidget{parent},
    lines_count(0),
    address_first(_FFFF),
    address_last(_FFFF),
    max_lines(0),
    data_valid(false)
{
    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(font);
    char_width = fm.horizontalAdvance('0');
    font_height = fm.height();
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

void DisAsmArea::go_to(unsigned int address)
{
    this->address = address;
    data_valid = false;
    update();
}

void DisAsmArea::update_data()
{
    screen_size = size().height() / font_height - 1;
    if ( (address >= address_first) && (address <= address_last) )
    {
        //TODO: goto inside existing lines
        unsigned int screen_first = lines[first_line].address;
        unsigned int screen_last = lines[first_line+screen_size-1].address;
        if ( (address >= screen_first) && (address <= screen_last) )
        {
            for (unsigned int i = first_line; i < first_line+screen_size; i++)
                if (lines[i].address == address){
                    cursor_line = i-first_line;
                    this->update();
                    return;
                }
        }
    } else {
        lines_count = 0;

        unsigned int a = address;
        uint8_t buffer[15];

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
            lines[i].code = bytes_dump(&buffer, c);
            a += c;
        }
        lines_count = screen_size;
        address_first = lines[0].address;
        address_last = lines[lines_count-1].address;
        first_line = 0;
        cursor_line = 0;
    }
    data_valid = true;
}

void DisAsmArea::paintEvent(QPaintEvent *event)
{
    if (!data_valid) update_data();

    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));

    unsigned int lines_count = size().height() / font_height - 1;

    for (unsigned int i=0; i < lines_count; i++)
    {
        unsigned int line = first_line + i;
        if (line < lines_count)
        {
            unsigned int x = 20;
            unsigned int y = this->font_height * (i+1);
            if (i == cursor_line)
            {
                painter.setPen(SELECTION);
                painter.fillRect(0, y-font_height+3, size().width(), font_height, SELECTION_BACK);
            } else {
                painter.setPen(QColor(0,0,0));
            }
            QString prefix_str = (lines[line].address == cpu->get_pc())?"►":" ";
            painter.drawText(x, y, prefix_str);
            x += char_width * (1+1);

            QString address_str = QString("%1").arg(lines[line].address, 4, 16, QChar('0')).toUpper() + " :";
            painter.drawText(x, y, address_str);
            x += char_width * (6+1);

            painter.drawText(x, y, lines[line].code);
            x += char_width * (3*disasm->max_command_length+4);

            painter.drawText(x, y, lines[line].command);
        }
    }
}
