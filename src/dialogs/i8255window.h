// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: 8255 debug window, header

#pragma once

#include "dialogs/genericdbgwnd.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/emulator.h"
#include <QDialog>

namespace Ui {
class I8255Window;
}

class I8255Window : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit I8255Window(QWidget *parent = nullptr);
    I8255Window(QWidget *parent, Emulator * e, ComputerDevice * d);
    ~I8255Window();

private:
    Ui::I8255Window *ui;

    Emulator * e;
    I8255 * d;
    QTimer * timer;

private slots:
    void update();

};

GenericDbgWnd *CreateI8255Window(QWidget *parent, Emulator * e, ComputerDevice * d);

