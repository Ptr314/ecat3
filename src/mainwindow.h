// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main window header

#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QToolButton>
#include <QTranslator>

#include "emulator/emulator.h"
#include "emulator/debug.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/tape.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    Emulator *e;

protected:
    void keyPressEvent( QKeyEvent * event) override;
    void keyReleaseEvent( QKeyEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void paintEvent(QPaintEvent * event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void load_config(QString file_name, bool set_default);

    void onDeviceMenuCalled(unsigned int i);

    void on_action_Cold_restart_triggered();

    void on_action_Soft_restart_triggered();

    void set_volume(int value);
    void set_mute(bool muted);

    void on_action_Select_a_machine_triggered();

    void on_actionOpen_triggered();

    void on_actionDebugger_triggered();

    //void show_screen();

    void on_action_Exit_triggered();

    void fdd_open(unsigned int n);
    void fdd_eject(unsigned int n);
    void fdd_wp(unsigned int n);
    void fdd_write(unsigned int n);

    void update_fdds();

    void on_actionScreenshot_triggered();

    void on_actionAbout_triggered();

    void on_actionTape_triggered();

signals:
    void send_a_key(QKeyEvent *event, bool press);
    void send_volume(int value);
    void send_muted(bool muted);
    void send_reset(bool cool);
    void send_resize();
    void send_stop();

private:
    Ui::MainWindow *ui;

    QWidget * screen;
    VideoRenderer * renderer;

    QSettings * m_settings;
    QTranslator translator;
    QTranslator qtTranslator;

    DebugWindowsManager * DWM;

    QSlider * volume;
    QToolButton * mute;

    QToolButton * fdd_button[8];
    // QToolButton * tape_button = nullptr;
    QAction * tape_action = nullptr;
    QAction * buttons_separator = nullptr;
    QMenu * fdd_menu[2];
    std::vector<FDD*> fdds;
    //FDC * fdc;
    // TapeRecorder * tape;
    unsigned int fdds_found = 0;
    QTimer * fdd_timer;
    QString last_path;
    bool fdd_blinker;

    bool first_show = true;
    QString first_config;

    void switch_language(const QString &lang, bool init);
    void add_languages();

    void CreateDevicesMenu();
    void UpdateToolbar();
    void CreateScreenMenu();
    void CreateFDDMenu(unsigned int n);

    void set_title();

};
