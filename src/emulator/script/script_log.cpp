// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Deferred log file for the scripting engine, source

#include <iostream>

#include "emulator/script/script_log.h"
#include "emulator/utils.h"
#include "libs/dsk_tools/src/utils.h"

ScriptLog::ScriptLog(const std::string &script_file):
      m_path(dsk_tools::get_file_path(script_file))
    , m_name(dsk_tools::get_file_basename(script_file))
    , m_failed(false)
{
    if (m_name.empty()) m_name = "script";
}

ScriptLog::~ScriptLog()
{
    if (m_file) m_file->close();
}

void ScriptLog::set_name(const std::string &name)
{
    //Only meaningful before the file has been created
    if (!m_file && !name.empty()) m_name = name;
}

bool ScriptLog::is_open() const
{
    return static_cast<bool>(m_file);
}

const std::string & ScriptLog::get_file_name() const
{
    return m_file_name;
}

void ScriptLog::open()
{
    m_file_name = m_path + m_name + "-" + timestamp_string() + ".log";

    std::unique_ptr<dsk_tools::UTF8_ofstream> f(new dsk_tools::UTF8_ofstream(m_file_name, std::ios::binary));
    if (f->is_open())
        m_file = std::move(f);
    else {
        m_failed = true;
        std::cerr << "Unable to create a script log file: " << m_file_name << std::endl;
    }
}

void ScriptLog::write(const std::string &s)
{
    if (!m_file) {
        if (m_failed) return;
        open();
        if (!m_file) return;
    }

    std::string line = s + "\x0D\x0A";
    m_file->write(line.c_str(), static_cast<std::streamsize>(line.size()));

#if defined(_WIN32) && !defined(_MSC_VER)
    //The MinGW implementation writes through WriteFile and is not buffered
#else
    m_file->flush();
#endif
}
