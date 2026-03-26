// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Irisha display controller device

#pragma once

#include "emulator/core.h"

class IrishaDisplay: public GenericDisplay
{
private:
    unsigned m_mode;
    unsigned m_color;
    unsigned m_page;
    unsigned m_base_address{};
    uint32_t m_fore_color;
    uint32_t m_back_color;
    unsigned m_mode_index;
    unsigned m_page_size;

    Port * port_mode{};
    Port * port_color{};
    Port * port_page{};
    RAM * vram{};


    void render_mono(unsigned address) const;
    void render_color(unsigned address) const;
    void render_blank() const;

protected:
    void render_all(bool force_render) override;

public:
    IrishaDisplay(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_renderer(VideoRenderer &vr) override;

    void clock(unsigned int counter) override;
    dsk_tools::Result load_config(SystemData *sd) override;

    void memory_callback(unsigned int callback_id, unsigned int address) override;

    void get_screen_constraints(unsigned int * sx, unsigned int * sy) override;
};

ComputerDevice * create_irisha_display(InterfaceManager *im, EmulatorConfigDevice *cd);
