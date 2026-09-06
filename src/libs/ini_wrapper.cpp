// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: C++11-compatible INI settings wrapper (backends: mINI / QSettings)

#include "ini_wrapper.h"

#ifdef USE_MINI_INI
// ======================== mINI backend (C++17) ========================

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    // libstdc++ already defines it, and redefining it is a warning
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <process.h>
    #define ECAT_INI_PID _getpid()
#else
    #include <unistd.h>
    #define ECAT_INI_PID getpid()
#endif

#include "ini.h"

struct IniSettingsData {
    std::string filename;
    mINI::INIStructure data;
    // False when the file is there but would not open. Saving then would put an
    // empty ini over a full one, which is the very thing the atomic write below
    // is there to prevent
    bool loaded = true;
};

IniSettings::IniSettings()
    : d(new IniSettingsData())
{
}

IniSettings::IniSettings(const std::string &filename)
    : d(new IniSettingsData())
{
    open(filename);
}

IniSettings::~IniSettings()
{
    delete d;
}

// Reading fails on a file that exists: while another emulator moves its freshly
// written ini over this name, the name cannot be opened at all. An instance that
// took that for an empty ini would go on with no settings and then save that
// emptiness over everyone else's, so the read is retried while the file is there.
void IniSettings::open(const std::string &filename)
{
    d->filename = filename;
    d->loaded = true;

    mINI::INIFile file(filename);
    for (int attempt = 0; attempt < 40; attempt++)
    {
        if (file.read(d->data)) return;
        // Missing outright is not a failure - the first run creates it
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(filename), ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    d->loaded = false;
}

bool IniSettings::has(const std::string &section, const std::string &ident) const
{
    return d->data.has(section) && d->data[section].has(ident);
}

std::string IniSettings::get(const std::string &section, const std::string &ident, const std::string &def_val) const
{
    if (has(section, ident)) {
        std::string val = d->data[section][ident];
        // mINI preserves surrounding quotes; strip them for QSettings compatibility
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        return val;
    }
    return def_val;
}

void IniSettings::set(const std::string &section, const std::string &ident, const std::string &value)
{
    d->data[section][ident] = value;
}

// Replaces one file with another in a single step. std::filesystem::rename is
// specified to do it, but on Windows GCC's implementation goes through _wrename,
// which refuses to overwrite an existing name, so there the call is made by hand.
//
// Windows also refuses to replace a file somebody has open, and somebody here is
// another emulator reading the same ini as it starts. That lasts as long as it
// takes to read half a kilobyte, so the move is retried for a moment before it
// is given up on.
static bool replace_file(const std::filesystem::path &from, const std::filesystem::path &to)
{
    for (int attempt = 0; ; attempt++)
    {
#ifdef _WIN32
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) return true;
#else
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        if (!ec) return true;
#endif
        if (attempt >= 40) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// mINI writes the ini in place: it truncates the file and only then fills it
// in. Another process reading the ini inside that window gets an empty one and
// writes that emptiness back on its own way out, so two emulators started side
// by side lose whole sections - the test runner with -j 4 used to lose
// [TapeFiles], after which no script could load a tape. The new contents go
// into a file next to the target and are moved over it in one step, so a reader
// sees one complete version or the other and never a half of one.
void IniSettings::save()
{
    namespace fs = std::filesystem;

    if (d->filename.empty() || !d->loaded) return;

    const fs::path target(d->filename);
    fs::path temp(d->filename);
    // The name has to be unique per process: several of them save at once
    temp += ".new" + std::to_string(ECAT_INI_PID);

    std::error_code ec;
    // Copying the original first is what keeps its comments and the order of
    // its sections: mINI rewrites the lines of the file it is given, and here
    // that file is a copy of the target
    if (fs::exists(target, ec))
        fs::copy_file(target, temp, fs::copy_options::overwrite_existing, ec);

    bool written = false;
    {
        mINI::INIFile file(temp);
        written = file.write(d->data);
    }

    if (written && replace_file(temp, target)) return;

    // Nothing is written in place as a fallback: what is saved here is the
    // volume, the last machine and the window options, and losing one such save
    // costs nothing next to leaving a truncated ini behind for everybody
    fs::remove(temp, ec);
}

#else
// ======================== QSettings backend ========================

#include <QSettings>
#include <QString>

struct IniSettingsData {
    QSettings *qs;
};

IniSettings::IniSettings()
    : d(new IniSettingsData{nullptr})
{
}

IniSettings::IniSettings(const std::string &filename)
    : d(new IniSettingsData{nullptr})
{
    open(filename);
}

IniSettings::~IniSettings()
{
    delete d->qs;
    delete d;
}

void IniSettings::open(const std::string &filename)
{
    delete d->qs;
    d->qs = new QSettings(QString::fromStdString(filename), QSettings::IniFormat);
}

bool IniSettings::has(const std::string &section, const std::string &ident) const
{
    if (!d->qs) return false;
    return d->qs->contains(QString::fromStdString(section + "/" + ident));
}

std::string IniSettings::get(const std::string &section, const std::string &ident, const std::string &def_val) const
{
    if (!d->qs) return def_val;
    return d->qs->value(QString::fromStdString(section + "/" + ident), QString::fromStdString(def_val)).toString().toStdString();
}

void IniSettings::set(const std::string &section, const std::string &ident, const std::string &value)
{
    if (!d->qs) return;
    d->qs->setValue(QString::fromStdString(section + "/" + ident), QString::fromStdString(value));
}

void IniSettings::save()
{
    if (d->qs) d->qs->sync();
}

#endif