// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A socket that one of several input devices is plugged into

#pragma once

#include <vector>

#include "emulator/core.h"

// An input device that can be unplugged: a joystick or a mouse sharing one
// socket of a machine. An unplugged device ignores the host and never drives
// its output, so the lines it shares with the other devices belong to them.
class PluggableDevice : public ComputerDevice
{
public:
    PluggableDevice(InterfaceManager *im, EmulatorConfigDevice *cd);

    void set_plugged(bool on);
    bool is_plugged() const { return m_plugged; }

    // Untranslated name for the socket's option list, see QT_TRANSLATE_NOOP
    virtual const char * plug_title() const = 0;

protected:
    // Called after the state has changed: a device that has been plugged in
    // drives its lines again, an unplugged one releases what the host holds
    virtual void plug_changed() = 0;

    // Plugged by default, so that a machine without a socket works as before
    bool m_plugged;
};

// The socket itself. Its only option picks the device plugged into it, or
// none; the choice is kept per machine like every other device option.
class Connector : public ComputerDevice
{
private:
    std::vector<PluggableDevice*> m_devices;
    unsigned int m_selected;            // 0 - nothing, n - device n-1
    std::string m_icon;                 // picture next to the option list, empty - the GUI's own

    void apply();

public:
    Connector(InterfaceManager *im, EmulatorConfigDevice *cd);

    emulator::Result load_config(SystemData *sd) override;

    DeviceOptions get_device_options() override;
    void set_device_option(unsigned option_id, unsigned value_id) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_connector(InterfaceManager *im, EmulatorConfigDevice *cd);
