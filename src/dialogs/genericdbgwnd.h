// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common ancestor foe debug windows, header

#pragma once

#include <QDialog>

class GenericDbgWnd : public QDialog
{
    Q_OBJECT
public:
    GenericDbgWnd(QWidget *parent);

signals:
    void data_changed(GenericDbgWnd * src);

public slots:
    virtual void update_view();
};
