// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Emulator config functions, header

#pragma once

#include <QObject>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "definitions.h"

struct EmulatorConfigParameter {
    std::string name;
    std::string left_range;
    std::string value;
    std::string right_range;
    std::string right_extended;
};

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const auto skip_empty_parts = Qt::SkipEmptyParts;
#else
    const auto skip_empty_parts = QString::SkipEmptyParts;
#endif

class EmulatorConfigDevice: public QObject
{
    Q_OBJECT

public:
    EmulatorConfigDevice(std::string name, std::string type);
    ~EmulatorConfigDevice();

    std::string name;
    std::string type;
    std::vector<EmulatorConfigParameter> parameters;

    void add_parameter(std::string name, std::string left_range, std::string value, std::string right_range, std::string right_extended);
    std::string extended_parameter(unsigned int i, std::string expected_name);
    EmulatorConfigParameter get_parameter(std::string name, bool required = true);
    EmulatorConfigParameter get_parameter(unsigned int id);
};

class EmulatorConfig: public QObject
{
    Q_OBJECT

public:
    EmulatorConfig();
    EmulatorConfig(std::string file_name);
    ~EmulatorConfig();

    dsk_tools::Result load_from_file(std::string file_name, bool system_only = false);
    void free_devices();

    EmulatorConfigDevice * get_device(int i);
    EmulatorConfigDevice * get_device(const std::string& name);
    unsigned int get_devices_count() const { return devices.size(); }

private:
    std::vector<std::unique_ptr<EmulatorConfigDevice>> devices;

    std::string read_next_entity(std::string *config, std::string stop = "");
    std::string read_extended_entity(std::string *config, std::string stop);
    EmulatorConfigDevice *add_device(std::string device_name, std::string device_type);
};
