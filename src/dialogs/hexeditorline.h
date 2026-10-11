// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Hex editor widget, header

#pragma once

#include <QLineEdit>

class HexEditorLine : public QLineEdit
{
    Q_OBJECT
public:
    HexEditorLine(QWidget *parent);
    HexEditorLine(QWidget *parent, QFont font, unsigned int char_width, unsigned int font_height);

    //The base and the number of digits of a cell: a byte or a word, in hex or octal
    void set_format(int base, int digits);

signals:
    void tab_pressed();
    void return_pressed();
    void esc_pressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    unsigned int m_char_width;
    unsigned int m_font_height;
};
