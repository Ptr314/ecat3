// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder window, header

#pragma once

#include <QDialog>

#include "dialogs/genericdbgwnd.h"
#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/devices/common/tape.h"

namespace Ui {
class TapeRecorderWindow;
}

class TapeRecorderWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit TapeRecorderWindow(QWidget *parent = nullptr);
    TapeRecorderWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

    ~TapeRecorderWindow();


protected:
    QIcon btnIconOff;
    QIcon btnIconOn;
    QIcon btnIconEjectOff;
    QIcon btnIconEjectOn;

    bool is_playing;
    bool is_paused;

    void play_pause();

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::TapeRecorderWindow *ui;

    Emulator * e;
    TapeRecorder * d;

    QPoint dragPosition;

    QTimer update_timer;

    QString loaded_file;

private slots:
    void set_mute(bool muted);
    void update_counter();
    void on_buttonEject_released();
    void on_buttonRewind_pressed();
    void on_buttonRewind_released();
    void on_buttonForward_pressed();
    void on_buttonForward_released();
    void on_buttonEject_pressed();
    void on_buttonPlay_clicked();
    void on_buttonStop_clicked();
    void on_toolButton_clicked();
    void on_buttonPause_clicked();
    void on_buttonMute_clicked();
    void on_buttonRewind_clicked();
};

GenericDbgWnd * CreateTapeWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
