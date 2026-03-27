// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Qt-dependent utility functions (not part of the emulator core)

#pragma once

#include <vector>
#include <string>
#include <QString>

QString pad_string(QString s, QChar c, int len, bool from_left = true);

unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp);

std::string md2html(const std::string &md);