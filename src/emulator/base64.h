// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Base64 encoder, header

#pragma once

#include <string>
#include <vector>

//Standard base64 with padding, no line breaks: what an MCP image content block
//expects, and what the configuration editor splits into lines for inline data.
//Decoding is dsk_tools::base64_decode()
std::string base64_encode(const std::vector<unsigned char> &data);
