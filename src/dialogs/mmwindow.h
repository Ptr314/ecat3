// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory manager window, header

#pragma once

#include <QDialog>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

namespace Ui {
class MemoryMapperWindow;
}

class MemoryMapperWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit MemoryMapperWindow(QWidget *parent = nullptr);
    explicit MemoryMapperWindow(QWidget *parent, Emulator * e, ComputerDevice * device);
    ~MemoryMapperWindow();

private slots:
    void on_pushButton_clicked();

    void on_process_button_clicked();

    void on_baseButton_toggled(bool checked);

    void on_wordButton_toggled(bool checked);

public slots:
    virtual void update_view() override;

private:
    void apply_view();

    Ui::MemoryMapperWindow *ui;

    Emulator * e;
    ComputerDevice * d;
    bool has_device;
};

GenericDbgWnd *CreateMMWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
