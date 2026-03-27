// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug windows manager, header

#pragma once

#include <algorithm>
#include <vector>

#include "dialogs/genericdbgwnd.h"
#include "emulator.h"

using DebugWndCreateFunc = GenericDbgWnd * (QWidget * parent, Emulator * e, ComputerDevice * d);

struct DebugWndFuncData {
    std::string device_type;
    DebugWndCreateFunc * f;
};

class DebugWindowsManager
{
private:
    unsigned int count;
    DebugWndFuncData WndFuncData[100];
    std::vector<GenericDbgWnd*> windows;

public:
    DebugWindowsManager():count(0){};

    void register_debug_window(const std::string &device_type, DebugWndCreateFunc * f)
    {
        WndFuncData[this->count++] = {device_type, f};
    };

    DebugWndCreateFunc * get_create_func(const std::string &device_type)
    {
        for (unsigned int i=0; i < this->count; i++)
            if (WndFuncData[i].device_type == device_type)
                return (WndFuncData[i].f);
        return nullptr;
    }

    void add_window(GenericDbgWnd * w)
    {
        windows.push_back(w);
    }

    void remove_window(GenericDbgWnd * w)
    {
        windows.erase(std::remove(windows.begin(), windows.end(), w), windows.end());
    }

    void data_changed(GenericDbgWnd * src){
        for (size_t i = 0; i < windows.size(); i++)
            windows[i]->update_view();
    }
};