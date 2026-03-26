// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: C++11-compatible INI settings wrapper (backends: mINI / QSettings)

#pragma once

#include <string>

struct IniSettingsData;

class IniSettings {
public:
    IniSettings();
    explicit IniSettings(const std::string &filename);
    ~IniSettings();

    IniSettings(const IniSettings&) = delete;
    IniSettings& operator=(const IniSettings&) = delete;

    void open(const std::string &filename);
    bool has(const std::string &section, const std::string &ident) const;
    std::string get(const std::string &section, const std::string &ident, const std::string &def_val = "") const;
    void set(const std::string &section, const std::string &ident, const std::string &value);
    void save();

private:
    IniSettingsData *d;
};