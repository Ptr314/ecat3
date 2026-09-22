// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder window, header

#pragma once

#include <QDialog>
#include <QTimer>

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

    bool is_playing = false;
    bool is_paused = false;
    bool is_recording = false;

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

    //Лента движется - воспроизведением или перемоткой. Счетчик идет в обоих
    //случаях, а кнопка воспроизведения нажата только в первом
    bool is_moving = false;
    bool is_fast = false;       // Ролики на перемотке крутятся быстрее

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
    void on_toolButton_clicked();
    void on_buttonPause_clicked();
    void on_buttonMute_clicked();
    void on_buttonRewind_clicked();
    void on_buttonRec_clicked();
    void tape_mode_changed(unsigned int new_mode);
    //Окно ничего не помнит само: состояние берется у устройства - и при
    //открытии, и каждый раз, когда оно меняется. Иначе лента, запущенная
    //машиной или сценарием до открытия окна, показывалась бы остановленной
    void sync_from_device();
    //Выгрузка записанного: у обычной ленты это то, что машина наговорила,
    //у ленты-накопителя - вся кассета (см. TapeRecorder::get_save_data)
    void save_recording();
    //Ролики и счетчик: fast - перемотка, они крутятся быстрее
    void show_movement(bool moving, bool fast);
};

GenericDbgWnd * CreateTapeWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
