// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Hex editor widget, source

#include "hexeditorline.h"

#include <QKeyEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

HexEditorLine::HexEditorLine(QWidget *parent, QFont font, unsigned int char_width, unsigned int font_height):
    QLineEdit(parent),
    m_char_width(char_width),
    m_font_height(font_height)
{
    setAlignment(Qt::AlignHCenter);
    setFont(font);
    setStyleSheet(
        "QLineEdit {"
            "border: 1px solid white;"
            "padding: 0;"
            "margin: 0;"
            "background: #00AAAA;"
            "selection-background-color: #000080;"
        "}"
    );

    set_format(16, 2);
}

void HexEditorLine::set_format(int base, int digits)
{
    setFixedSize(m_char_width*(digits+1), m_font_height+2);
    setMaxLength(digits);
    const QString chars = (base == 8) ? "[0-7]" : "[0-9A-Fa-f]";
    const QValidator * old = validator();
    setValidator(new QRegularExpressionValidator(
        QRegularExpression(QString("%1{0,%2}").arg(chars).arg(digits)), this));
    delete old;
}

void HexEditorLine::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit return_pressed();
        return;
    } else
    if (event->key() == Qt::Key_Escape) {
        emit esc_pressed();
        return;
    };

    QLineEdit::keyPressEvent(event);
}

 bool HexEditorLine::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
        if (dynamic_cast<QKeyEvent*>(event)->key() ==  Qt::Key_Tab) {
            emit tab_pressed();
            return true;
        }
    return QLineEdit::event(event);
}
