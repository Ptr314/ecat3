// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the AZBK controller's memory mapper, header

#pragma once

#include <string>
#include <vector>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"
#include "emulator/devices/specific/azbk.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QTreeWidget;
class QTimer;
class QVBoxLayout;
class MemoryPanel;
struct MemorySource;

//The 16 windows of the AZBK as the processor sees them now, the AZ pages the
//window registers name whether the windows answer or not, or the whole AZ
//memory. A table says what each window holds and who answers a read and a
//write there (the board's own route()), or for a run of AZ pages what they are
//and where the processor sees them. The window only looks: the mapper stays
//as the machine set it
class AZBKWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    AZBKWindow(QWidget *parent, Emulator * e, AZBK * d);
    ~AZBKWindow() override;

public slots:
    void update_view() override;

private:
    enum View { VIEW_NOW = 0, VIEW_PAGES = 1, VIEW_MEMORY = 2 };

    //The mapper as the board had it at the last read, taken in one pass on the
    //emulation thread
    struct State {
        unsigned int win[16];
        unsigned int rd[16];            //route() of a read and a write of the window
        unsigned int wr[16];
        bool ro[16];                    //the window takes no write (0177342 or a ROM page)
        std::string below[16];          //what the machine has under the window
        unsigned int active;
        unsigned int readonly;
        unsigned int shadow;
        unsigned int control;
        unsigned int copy130;
        unsigned int copy716;
        bool start;
        bool smk_flag;
        State();
        bool operator==(const State &o) const;
    };

    Emulator * e;
    AZBK * d;
    CPU * m_cpu;
    Memory * m_ram;
    int m_base;
    std::vector<AZBK::Slot> m_slots;
    std::string m_logo;

    QComboBox * m_view;
    QSpinBox * m_page;
    QLabel * m_registers;
    QTreeWidget * m_layout;
    QVBoxLayout * m_dump_box;
    MemoryPanel * m_panel;
    int m_panel_view;
    QTimer * m_save_timer;      //the choice goes to the ini a second after the last change

    State m_state;

    QString page_number(unsigned int page) const;
    QString describe_page(unsigned int page) const;
    QString az_text(unsigned int page, unsigned int offset) const;
    QString seen_at(unsigned int page) const;
    QString read_text(unsigned int n) const;
    QString write_text(unsigned int n) const;
    MemorySource source_now();
    MemorySource source_pages();
    MemorySource source_memory();
    void read_board();
    void show_registers();
    void show_selection();
    void load_choice();
    void save_choice();
    void write_choice();
    void show_layout();
    void rebuild();
};

GenericDbgWnd * CreateAZBKWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
