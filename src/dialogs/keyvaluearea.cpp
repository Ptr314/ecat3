#include <QPainter>

#include "dialogs/dialogs.h"
#include "keyvaluearea.h"

KeyValueArea::KeyValueArea(QWidget *parent)
    : DOSFrame{parent}
{
//    QFont font(FONT_NAME, FONT_SIZE);
    QFontMetrics fm(*font);
//    char_width = fm.horizontalAdvance('0');
    font_height = fm.height();
}

void KeyValueArea::set_data(QList<QString> newlist)
{
    list = newlist;
    update();
}

void KeyValueArea::paintEvent(QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    QPainter painter(this);
    //painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);
    painter.setFont(*font);

    painter.setPen(TEXT_COLOR);

    for (unsigned int i=0; i<list.size(); i++)
    {
        unsigned int x = 20;
        unsigned int y = font_height * (i+2);
        painter.drawText(x, y, list.at(i));
    }
}
