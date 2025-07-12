// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port debug window, header

#pragma once

#include "dialogs/genericdbgwnd.h"
#include "emulator/core.h"
#include "emulator/emulator.h"
#include <QDialog>

namespace Ui {
class PortWindow;
}

class PortWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit PortWindow(QWidget *parent = nullptr);
    PortWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
    ~PortWindow();

private:
    Ui::PortWindow *ui;

    Emulator * e;
    Port * d;
    QTimer * timer;

private slots:
    void update();
};

GenericDbgWnd *CreatePortWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
