#include <QPainter>

#include "dosframe.h"
#include "dialogs/dialogs.h"

DOSFrame::DOSFrame(QWidget *parent)
    : QWidget{parent}
{
    font = new QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT);
    QFontMetrics fm(*font);
    char_width = static_cast<float>(fm.horizontalAdvance("01234567890123456789012345678901234567890123456789")) / 50;
    font_height = fm.height();
}

void DOSFrame::set_frame(bool top, bool right, bool bottom, bool left, QString chars)
{
    frame_top = top;
    frame_right = right;
    frame_bottom = bottom;
    frame_left = left;
    frame_chars = chars;
}


void DOSFrame::paintEvent([[maybe_unused]] QPaintEvent *event)
{
    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);
    painter.setFont(*font);
    painter.setPen(QColor(255,255,255));
    int sx = floor(static_cast<float>(size().width()) / char_width);
    int sy = size().height() / font_height;
    QString s = "";
    int c = 0;
    if (frame_top) {
        if (frame_left) {
            s = frame_chars.at(0);
            c++;
        }
        if (frame_right) c++;

        for (int i=0; i<sx-c; i++)
            s += frame_chars.at(1);

        if (frame_right) s += frame_chars.at(2);
        painter.drawText(0, font_height, s);
    }
    c = 0;
    int y = 1;
    if (frame_top) {y++; c++;};
    if (frame_bottom) c++;

    if (frame_left) {
        QString f = frame_chars.at(3);
        for (int i=0; i<sy-c; i++)
            painter.drawText(0, font_height*(y+i), f);
    }
    if (frame_right) {
        QString f = frame_chars.at(5);
        for (int i=0; i<sy-c; i++)
            painter.drawText(floor(char_width*(sx-1)), font_height*(y+i), f);
    }
    c = 0;
    s = "";
    if (frame_top) {
        if (frame_left) {
            s = frame_chars.at(6);
            c++;
        }
        if (frame_right) c++;

        for (int i=0; i<sx-c; i++)
            s += frame_chars.at(7);

        if (frame_right) s += frame_chars.at(8);
        painter.drawText(0, font_height*sy, s);
    }
}
