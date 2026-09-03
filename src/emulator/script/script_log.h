// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Deferred log file for the scripting engine, header

#pragma once

#include <memory>
#include <string>

//Only the UTF-8 stream wrappers are needed here. The umbrella dsk_tools.h is
//deliberately not included: it drags in headers that clash with dialogs.h.
#include "host_helpers.h"

// A log file for a running script.
//
// The file is created lazily, on the first write, so a script without LOG
// commands leaves no files behind. Every line reaches the disk immediately, so
// the log survives an abnormal termination of the emulator.
//
// The name is "<script name>-YYYY-MM-DD-HH-mm-SS.log" in the script directory.
class ScriptLog
{
public:
    explicit ScriptLog(const std::string &script_file);
    ~ScriptLog();

    void set_name(const std::string &name);     //Command LOGFILE
    void write(const std::string &s);
    bool is_open() const;
    const std::string & get_file_name() const;

private:
    std::string m_path;             //Directory of the script
    std::string m_name;             //Base name, without a timestamp
    std::string m_file_name;        //Full name, filled in when the file is created
    //UTF8_ofstream has no default constructor on MinGW, so it is created lazily
    std::unique_ptr<dsk_tools::UTF8_ofstream> m_file;
    bool m_failed;

    void open();
};
