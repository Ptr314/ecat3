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

//menu_text: the item the window adds to the Debug menu when the machine has
//such a device, a source text of the "DebugMenu" context (QT_TRANSLATE_NOOP);
//nullptr - only the Devices menu opens it
struct DebugWndFuncData {
    std::string device_type;
    DebugWndCreateFunc * f;
    const char * menu_text;
};

class DebugWindowsManager
{
private:
    unsigned int count;
    DebugWndFuncData WndFuncData[100];
    std::vector<GenericDbgWnd*> windows;

public:
    DebugWindowsManager():count(0){};

    void register_debug_window(const std::string &device_type, DebugWndCreateFunc * f, const char * menu_text = nullptr)
    {
        WndFuncData[this->count++] = {device_type, f, menu_text};
    };

    DebugWndCreateFunc * get_create_func(const std::string &device_type)
    {
        for (unsigned int i=0; i < this->count; i++)
            if (WndFuncData[i].device_type == device_type)
                return (WndFuncData[i].f);
        return nullptr;
    }

    const char * get_menu_text(const std::string &device_type)
    {
        for (unsigned int i=0; i < this->count; i++)
            if (WndFuncData[i].device_type == device_type)
                return (WndFuncData[i].menu_text);
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