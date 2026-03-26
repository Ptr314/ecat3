// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: C++11-compatible INI settings wrapper (backends: mINI / QSettings)

#include "ini_wrapper.h"

#ifdef USE_MINI_INI
// ======================== mINI backend (C++17) ========================

#include "ini.h"

struct IniSettingsData {
    std::string filename;
    mINI::INIStructure data;
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

void IniSettings::open(const std::string &filename)
{
    d->filename = filename;
    mINI::INIFile file(filename);
    file.read(d->data);
}

bool IniSettings::has(const std::string &section, const std::string &ident) const
{
    return d->data.has(section) && d->data[section].has(ident);
}

std::string IniSettings::get(const std::string &section, const std::string &ident, const std::string &def_val) const
{
    if (has(section, ident))
        return d->data[section][ident];
    return def_val;
}

void IniSettings::set(const std::string &section, const std::string &ident, const std::string &value)
{
    d->data[section][ident] = value;
}

void IniSettings::save()
{
    mINI::INIFile file(d->filename);
    file.write(d->data);
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