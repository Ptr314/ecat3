// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: DOS-like widget's header

#pragma once

#include <QObject>
#include <QWidget>

class DOSFrame : public QWidget
{
    Q_OBJECT

private:
    QString frame_chars;
    QString scroll_chars;
    unsigned int scroll_range;
    unsigned int scroll_position;

public:
    explicit DOSFrame(QWidget *parent = nullptr);
    void set_frame(bool top, bool right, bool bottom, bool left, QString chars="╔═╗║ ║╚═╝");
    void set_scroll(unsigned int range, unsigned int position, QString chars="▲▼■▒");

protected:
    bool frame_top;
    bool frame_right;
    bool frame_bottom;
    bool frame_left;
    QFont * font;
    unsigned int font_height;
    unsigned int  char_width;

    void paintEvent(QPaintEvent *event) override;


};
