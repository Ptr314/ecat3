// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Base64 encoder for the MCP image payload, header

#pragma once

#include <string>
#include <vector>

namespace mcp {

    //Standard base64 with padding, no line breaks: that is what an MCP image
    //content block expects
    std::string base64_encode(const std::vector<unsigned char> &data);

} // namespace mcp
