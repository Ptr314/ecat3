#include <QPainter>
#include <QFontDatabase>

#include "dialogs/dialogs.h"

#include "dumparea.h"

DumpArea::DumpArea(QWidget *parent)
    : QWidget{parent},
    d(nullptr),
    start_address(0)
{
    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(font);
    char_width = fm.horizontalAdvance('0');
    font_height = fm.height();
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
    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));
    painter.setPen(TEXT_COLOR);
    if (d != nullptr)
    {

        unsigned int lines_count = size().height() / font_height - 1;

        for (unsigned int i=0; i < lines_count; i++)
        {
            unsigned int address = start_address + i*16;
            if (address < d->get_size())
            {
                unsigned int x = 10;
                unsigned int y = font_height * (i+1);
                QString address_str = QString("%1 : ").arg(address, 4, 16, QChar('0')).toUpper();
                painter.drawText(x, y, address_str);
                x += address_str.length() * char_width;
                for (unsigned int j=0; j<16; j++)
                {
                    address = start_address + i*16 + j;
                    if (address < d->get_size())
                    {
                        uint8_t data = d->get_value(address);
                        QString data_str = QString("%1 ").arg(data, 2, 16, QChar('0')).toUpper();
                        painter.drawText(x + j*data_str.length()*char_width, y, data_str);
                        QString c = QString(*e->translate_char(data));
                        painter.drawText(x + char_width*(16 * data_str.length() + 5 + j), y, c);
                    }
                }
            }
        }
    }
}

