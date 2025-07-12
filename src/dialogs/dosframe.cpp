// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DOS-like widget's source

#include <QPainter>
#include <cmath>

#include "dosframe.h"
#include "dialogs/dialogs.h"
#include "emulator/core.h"

DOSFrame::DOSFrame(QWidget *parent)
    : QWidget{parent},
    frame_top(false),
    frame_right(false),
    frame_bottom(false),
    frame_left(false),
    frame_chars(""),
    scroll_range(0),
    scroll_position(0)

{
    font = new QFont(FONT_NAME, FONT_SIZE, FONT_WEIGHT);
    QFontMetrics fm(*font);

    #if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
        char_width = fm.width('0');
    #else
        char_width = fm.horizontalAdvance('0');
    #endif

    font_height = fm.height();

    //qDebug() << char_width << font_height;
}

void DOSFrame::set_frame(bool top, bool right, bool bottom, bool left, QString chars)
{
    frame_top = top;
    frame_right = right;
    frame_bottom = bottom;
    frame_left = left;
    frame_chars = chars;
}

void DOSFrame::set_scroll(unsigned int range, unsigned int position, QString chars)
{
    scroll_range = range;
    scroll_position = position;
    scroll_chars = chars;
    update();
}

void DOSFrame::paintEvent(MAYBE_UNUSED QPaintEvent *event)
{
    QPainter painter(this);

    painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);

    painter.setFont(*font);
    painter.setPen(QColor(255,255,255));
    int sx = size().width() / char_width;
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
        painter.drawText(0, font_height-4, s);
    }

    c = 0;
    int y = 1;
    if (frame_top) {y++; c++;};
    if (frame_bottom) c++;

    if (frame_left) {
        QString f = frame_chars.at(3);
        for (int i=0; i<sy-c; i++)
            painter.drawText(0, font_height*(y+i)-4, f);
    }

    if (frame_right) {
        QString f = frame_chars.at(5);
        if (scroll_range == 0)
        {
            for (int i=0; i<sy-c; i++)
                painter.drawText(char_width*(sx-1), font_height*(y+i)-4, f);
        } else {
            painter.fillRect(char_width*(sx-1), font_height*(y-1)-4, char_width-1, font_height*(sy-c)+4, SCROLL_BACK);
            painter.setPen(SCROLL_COLOR);
            int i = 0;
            painter.drawText(char_width*(sx-1), font_height*(y+i)-4, scroll_chars.at(0));
            int bar_pos = round((sy-c-3) * (static_cast<float>(scroll_position) / scroll_range )) + 1;
            while (i < sy-c - 2)
            {
                i++;
                painter.drawText(char_width*(sx-1), font_height*(y+i)-4, (i==bar_pos)?scroll_chars.at(2):scroll_chars.at(3));
            }
            painter.drawText(char_width*(sx-1), font_height*(y+i+1)-4, scroll_chars.at(1));

            painter.drawText(char_width*(sx-1), font_height*(y+bar_pos)-4, scroll_chars.at(2));
        }
    }

    painter.setPen(QColor(255,255,255));
    c = 0;
    s = "";
    if (frame_bottom) {
        if (frame_left) {
            s = frame_chars.at(6);
            c++;
        }
        if (frame_right) c++;

        for (int i=0; i<sx-c; i++)
            s += frame_chars.at(7);

        if (frame_right) s += frame_chars.at(8);
        painter.drawText(0, font_height*sy-4, s);
    }
}
