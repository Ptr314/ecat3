// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Serial ports window (output of the DL11 lines), header

#pragma once

#include <vector>

#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QComboBox>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTimer>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"
#include "emulator/devices/common/dl11.h"

//Вывод программы в порты DL11: вкладка на порт, в ней поле с выводом и запись
//потока в файл как есть. Байты снимает сам DL11 (set_tap/take_tap), окно
//забирает их по таймеру. Логирование живет, пока открыто окно
class SerialWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    SerialWindow(QWidget *parent, Emulator * e);
    ~SerialWindow();

    //Есть ли у машины порты DL11 - для пункта меню
    static bool machine_has_ports(Emulator * e);

    void select_device(ComputerDevice * d);

    //Идет запись в файл - спросить, можно ли ее прервать. Главное окно
    //спрашивает сам до смены машины и выхода и ставит set_confirmed(), чтобы
    //закрытие окна не спросило второй раз
    bool confirm_close();
    void set_confirmed() { confirmed = true; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum Mode { MODE_ORIGINAL, MODE_RECODED, MODE_BYTES, MODE_COUNT };

    struct Port {
        DL11 * dev = nullptr;
        QString name;
        QAction * log_action = nullptr;
        QAction * file_action = nullptr;
        QComboBox * charset = nullptr;
        QPlainTextEdit * text = nullptr;
        QFile * file = nullptr;
        QByteArray data;            //Принятое, для перерисовки при смене кодировки и режима
        //Кодировка, выбранная в списке: таблица dsk_tools или, если ее нет,
        //charmap машины. У КОИ-7 Н0/Н1 регистр переключают SO и SI
        const std::string (*table)[256] = nullptr;
        bool koi7 = false;
        unsigned koi7_high = 0;
        bool line_start = true;     //Поле кончается переводом строки или пустое
    };

    Emulator * e;
    std::vector<Port> ports;
    QTabWidget * tabs;
    QMenu * settings_menu;
    QAction * mode_actions[MODE_COUNT];
    QTimer poll_timer;
    int mode = MODE_RECODED;
    int base = 16;
    bool confirmed = false;
    std::vector<uint8_t> chunk;

    QWidget * create_page(size_t i);
    void set_logging(size_t i, bool on);
    void set_to_file(size_t i, bool on);
    void set_charset(size_t i, int index);
    void refill(Port & p);
    bool open_file(Port & p);
    void close_file(Port & p);
    void drain(Port & p);
    void stop_all();
    void poll();

    void append_text(Port & p, const QString & s);
    QString render(Port & p, const uint8_t * bytes, size_t count);
    void set_mode(int new_mode);
    QString folder();
    void choose_folder();
    std::string file_key(const Port & p);
    std::string charset_key(const Port & p);
};
