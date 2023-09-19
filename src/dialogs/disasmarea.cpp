#include <QPainter>

#include "emulator/utils.h"
#include "dialogs/dialogs.h"
#include "disasmarea.h"

DisAsmArea::DisAsmArea(QWidget *parent)
    : QWidget{parent},
    lines_count(0),
    address_first(_FFFF),
    address_last(_FFFF),
    max_lines(0)
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
    go_to(address);
}

void DisAsmArea::go_to(unsigned int address)
{
    if ( (address >= address_first) && (address >= address_last) )
    {
        //TODO: goto inside existing lines
    } else {
        lines_count = 0;
        max_lines = 25;

        unsigned int a = address;
        uint8_t buffer[15];

        for (unsigned int i = 0; i < max_lines; i++)
        {
            for (unsigned int j = 0; j < disasm->max_command_length; j++)
                buffer[j] = cpu->read_mem(a+j);
            QString s;
            unsigned int c = disasm->disassemle(&buffer, cpu->get_pc(), disasm->max_command_length, &s);
            lines[i].address = a;
            lines[i].command = s;
            lines[i].current = (a == 0xF803);
            lines[i].breakpoint = false;
            lines[i].code = bytes_dump(&buffer, c);
            a += c;
        }
    }

}

void DisAsmArea::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));

    unsigned int lines_count = size().height() / font_height - 1;

    for (unsigned int i=0; i < lines_count; i++)
    {
        if (i < max_lines)
        {
            unsigned int x = 20;
            unsigned int y = this->font_height * (i+1);
            if (lines[i].current)
            {
                painter.setPen(SELECTION);
                painter.fillRect(0, y-font_height+3, size().width(), font_height, SELECTION_BACK);
            } else {
                painter.setPen(QColor(0,0,0));
            }
            QString prefix_str = (lines[i].current)?"►":" ";
            painter.drawText(x, y, prefix_str);
            x += char_width * (1+1);

            QString address_str = QString("%1").arg(lines[i].address, 4, 16, QChar('0')).toUpper() + " :";
            painter.drawText(x, y, address_str);
            x += char_width * (6+1);

            painter.drawText(x, y, lines[i].code);
            x += char_width * (3*disasm->max_command_length+4);

            painter.drawText(x, y, lines[i].command);
        }
    }
}
