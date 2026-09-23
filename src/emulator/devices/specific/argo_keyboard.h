// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Арго ФВ-6511: опрос клавиатуры номером столбца на шине адреса

#pragma once

#include "emulator/core.h"

// Клавиатуру Арго процессор опрашивает командой IN A,(C) при C = $A1:
// номер столбца при этом оказывается на старших разрядах адреса порта, а
// младшие декодируются в сам порт. Матрица обычная, 11 столбцов на 8 строк с
// активным нулем, так что ее обслуживает scan-keyboard - этому устройству
// остается только взять столбец с шины адреса и отдать процессору строки.
//
// Старшие разряды приходят по ~port с ~io_address процессора: диспетчер
// памяти машины адресует порты байтом, как оно на плате и декодировано.
// Чтение выставляет столбец на ~scan, и ответ клавиатуры приходит на ~rows
// в том же обращении: распространение по интерфейсам синхронное.
class ArgoKeyboard: public AddressableDevice
{
private:
    Interface i_port;               // Полный адрес обращения к порту
    Interface i_scan;               // Номер столбца, на ~scan клавиатуры
    Interface i_rows;               // Ответ матрицы, активный ноль

public:
    ArgoKeyboard(InterfaceManager *im, EmulatorConfigDevice *cd);

    unsigned int get_value(unsigned int address) override;
    // Окно дампа и LOG читают регистр без побочных действий: столбец при этом
    // не трогаем, иначе взгляд на порт менял бы состояние машины
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_argo_keyboard(InterfaceManager *im, EmulatorConfigDevice *cd);
