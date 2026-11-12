// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Loader for files, header

#pragma once

#include "emulator/emulator.h"

//What a loader learned from the file it put into memory. A format with a
//header names where its data went; a flat dump does not, and leaves
//has_address false
struct LoadInfo {
    bool            has_address = false;
    unsigned int    address = 0;        //Where the first byte went
    unsigned int    size = 0;           //Bytes written
};

//Loads a file into the machine the way File > Open does, the loader picked by
//the extension. info, when given, gets what the loader knows of the result:
//the LOAD verb of a script hands the address on as ${load_address}
emulator::Result HandleExternalFile(Emulator * e, const std::string &file_name, LoadInfo * info = nullptr);

