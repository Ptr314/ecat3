// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Арго ФВ-6511: опрос клавиатуры номером столбца на шине адреса

#include "argo_keyboard.h"

ArgoKeyboard::ArgoKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_port(this, im, 16, "port", MODE_R)
    , i_scan(this, im, 4, "scan", MODE_W)
    , i_rows(this, im, 8, "rows", MODE_R)
{
    addresable_size = 0x10000;
}

unsigned int ArgoKeyboard::get_value(MAYBE_UNUSED unsigned int address)
{
    i_scan.change((i_port.value >> 8) & 0x0F);
    return i_rows.value & 0xFF;
}

unsigned int ArgoKeyboard::get_direct(MAYBE_UNUSED unsigned int address)
{
    return i_rows.value & 0xFF;
}

void ArgoKeyboard::set_value(MAYBE_UNUSED unsigned int address, MAYBE_UNUSED unsigned int value, MAYBE_UNUSED bool force)
{
}

std::vector<DeviceFieldInfo> ArgoKeyboard::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"column", "Matrix column of the last scan", false});
    r.push_back({"rows",   "Matrix answer, active low",       false});
    return r;
}

bool ArgoKeyboard::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "column") { out.numeric = true; out.width = 8; out.values.push_back(i_scan.value & 0x0F); return true; }
    if (field == "rows")   { out.numeric = true; out.width = 8; out.values.push_back(i_rows.value & 0xFF); return true; }
    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_argo_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new ArgoKeyboard(im, cd);
}
