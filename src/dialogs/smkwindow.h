// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the СМК-512 memory board, header

#pragma once

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QTreeWidget;
class QVBoxLayout;
class MemoryPanel;
class SMK512;
struct MemorySource;

//The window 0100000-0177777 of the СМК-512 as the processor sees it now, as it
//would see it in another mode and page, or one 32 KB page of the board.
//The window only looks: the board's mode stays as the machine set it
class SMKWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    SMKWindow(QWidget *parent, Emulator * e, SMK512 * d);

public slots:
    void update_view() override;

private:
    enum View { VIEW_NOW = 0, VIEW_MODE = 1, VIEW_PAGE = 2 };

    Emulator * e;
    SMK512 * d;
    CPU * m_cpu;
    int m_base;

    QComboBox * m_view;
    QComboBox * m_mode;
    QSpinBox * m_page;
    QLabel * m_register;
    QTreeWidget * m_layout;
    QVBoxLayout * m_dump_box;
    MemoryPanel * m_panel;

    unsigned int m_latched;     //the board's register when the view was built
    bool m_read_off;

    //The mode and page chosen by hand: the selectors show the board's own in
    //the view of the window now, and get these back when it is left
    int m_sel_mode;
    int m_sel_page;

    unsigned int selected_value() const;
    QString describe(unsigned int value, unsigned int offset) const;
    QString board_text(unsigned int value, unsigned int offset) const;
    MemorySource source_now();
    MemorySource source_mode(unsigned int value);
    MemorySource source_page(unsigned int page);
    void read_board();
    void show_register();
    void show_selection();
    void load_choice();
    void save_choice();
    void rebuild();
};

GenericDbgWnd * CreateSMKWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
