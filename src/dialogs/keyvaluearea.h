// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Key-value widget, header

#pragma once

#include <QWidget>

#include "dosframe.h"

class KeyValueArea : public DOSFrame
{
    Q_OBJECT
private:
    unsigned int font_height;
    unsigned int char_width;
    QList<QPair<QString, QString>> list;
    int key_len;
    int val_len;
    QString divider;

public:
    explicit KeyValueArea(QWidget *parent = nullptr);

    void set_data(QList<QPair<QString, QString>> newlist);
    void set_divider(QString new_divider);

protected:
    void paintEvent(QPaintEvent *event) override;

signals:

};

