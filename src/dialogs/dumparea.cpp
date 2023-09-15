#include <QPainter>
#include <QFontDatabase>

#include "dumparea.h"

//#define FONT_NAME "Consolas"
#define FONT_NAME "PxPlus IBM VGA9"
#define FONT_SIZE 11
#define FONT_WEIGHT 400 //600, 700

DumpArea::DumpArea(QWidget *parent)
    : QWidget{parent},
    d(nullptr),
    start_address(0)
{
    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(font);
    this->char_width = fm.horizontalAdvance('0');
    this->font_height = fm.height();
}

void DumpArea::set_data(Emulator * e, AddressableDevice * d, unsigned int start_address)
{
    this->d = d;
    this->e = e;
    this->start_address = start_address;
}

void DumpArea::paintEvent([[maybe_unused]] QPaintEvent *event)
{
    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), QColor(0,0,128));

    painter.setFont(QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT));
    painter.setPen(QColor(0,255,255));
    if (this->d != nullptr)
    {

        unsigned int lines_count = size().height() / this->font_height - 1;

        for (unsigned int i=0; i < lines_count; i++)
        {
            unsigned int address = this->start_address + i*16;
            if (address < this->d->get_size())
            {
                unsigned int x = 10;
                unsigned int y = this->font_height * (i+1);
                QString address_str = QString("%1 : ").arg(address, 4, 16, QChar('0')).toUpper();
                painter.drawText(x, y, address_str);
                x += address_str.length() * this->char_width;
                for (unsigned int j=0; j<16; j++)
                {
                    address = this->start_address + i*16 + j;
                    if (address < this->d->get_size())
                    {
                        uint8_t data = this->d->get_value(address);
                        QString data_str = QString("%1 ").arg(data, 2, 16, QChar('0')).toUpper();
                        painter.drawText(x + j*data_str.length()*this->char_width, y, data_str);
                        QString c = QString(*this->e->translate_char(data));
                        painter.drawText(x + this->char_width*(16 * data_str.length() + 5 + j), y, c);
                    }
                }
            }
        }
    }
}

