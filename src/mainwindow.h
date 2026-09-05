// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main window header

#pragma once

#include <QComboBox>
#include <QLabel>
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
    MainWindow(const QString &config_file = QString(), const QString &script_file = QString(), QWidget *parent = nullptr);
    ~MainWindow();

    //A script given on the command line may end with EXIT <code>. The code
    //is kept here for main(), which turns it into the status of the process
    int script_exit_code() const { return rec_exit_code; }

    Emulator *e;

#ifdef ENABLE_MCP
    //Starts the MCP server. Called before show(): in this mode the window
    //stays hidden until the client asks for a machine
    void enable_mcp(bool trace);

    //Called by McpBridge on the GUI thread. mcp_load_config() returns an error
    //message, empty on success
    QString mcp_load_config(const QString &file_name);
    QString mcp_current_machine() const;
    void    mcp_show_window();
#endif

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

    void on_actionRecOpen_triggered();
    void on_actionRecSave_triggered();
    void on_actionRecord_triggered();
    void on_actionRecPlay_triggered();
    void on_actionRecRewind_triggered();
    void on_actionRecStop_triggered();
    void on_actionRecPanel_toggled(bool checked);

    void rec_tick();

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

    std::unique_ptr<IniSettings> m_settings;
    QTranslator translator;
    QTranslator qtTranslator;

    DebugWindowsManager * DWM;

    QSlider * volume;
    QToolButton * mute;

    QToolButton * fdd_button[8];
    // QToolButton * tape_button = nullptr;
    QAction * tape_action = nullptr;
    QAction * buttons_separator = nullptr;
    QList<QAction*> option_toolbar_actions;
    QMenu * fdd_menu[8];
    std::vector<FDD*> fdds;
    //FDC * fdc;
    // TapeRecorder * tape;
    unsigned int fdds_found = 0;
    QTimer * fdd_timer;
    QString last_path;
    bool fdd_blinker;

    bool first_show = true;
    QString first_config;
    QString cur_config;                 //Configuration currently loaded

#ifdef ENABLE_MCP
    bool mcp_mode = false;
    bool mcp_screen_menu = false;       //CreateScreenMenu() must run once only
    QString mcp_load_error;             //Filled instead of showing a message box
    class McpBridge * mcp_bridge = nullptr;
#endif

    //Set from the command line, see main.cpp
    QString cmdline_config;
    QString script_file;

    QString resolve_startup_path(const QString &file_name) const;
    void start_script();

    //------------------------- Action recording ---------------------------//
    //The recording and the replay share one buffer, the one of the script
    //engine, so a session can be recorded, replayed, cut and continued
    enum RecState { RecIdle, RecRecording, RecPlaying, RecPaused };

    struct OptionCombo {
        std::string device;
        unsigned int option;
        QComboBox * combo;
    };

    RecState rec_state = RecIdle;
    QAction * rec_panel_separator = nullptr;   //Tool bar entries of the recording block
    QAction * rec_panel_widget = nullptr;
    QLabel * rec_label = nullptr;
    QTimer * rec_timer = nullptr;
    bool rec_ui_shown = false;          //The status bar block appears with the first recording or file
    bool rec_cmdline = false;           //A script from the command line: EXIT closes the window
    int rec_exit_code = 0;              //EXIT <code> of that script, returned from main()
    size_t rec_seen_pc = 0;             //Commands before it were checked for option changes
    uint64_t rec_total_ms = 0;
    QString rec_file;                   //Last opened or saved .ecat
    QList<OptionCombo> option_combos;   //Device option dropdowns of the tool bar

    bool rec_save();
    void rec_stop_all();
    void rec_update_ui();
    void rec_refresh_total();
    void rec_sync_option_combos(size_t from, size_t to);
    QString rec_machine_string() const;
    bool rec_machine_matches(const std::string &machine) const;
    static QString format_mmss(uint64_t ms);

    bool switch_language(const QString &lang, bool init);
    void add_languages();

    void CreateDevicesMenu();
    void UpdateToolbar();
    void CreateScreenMenu();
    void CreateFDDMenu(unsigned int n);

    void set_title();

};
