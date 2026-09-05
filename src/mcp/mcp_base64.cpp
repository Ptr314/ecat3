// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Base64 encoder for the MCP image payload, source

#include "mcp/mcp_base64.h"

namespace {
    const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

namespace mcp {

std::string base64_encode(const std::vector<unsigned char> &data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size())
    {
        unsigned int v = (static_cast<unsigned int>(data[i]) << 16)
                       | (static_cast<unsigned int>(data[i+1]) << 8)
                       |  static_cast<unsigned int>(data[i+2]);
        out += ALPHABET[(v >> 18) & 0x3F];
        out += ALPHABET[(v >> 12) & 0x3F];
        out += ALPHABET[(v >>  6) & 0x3F];
        out += ALPHABET[ v        & 0x3F];
        i += 3;
    }

    const size_t rest = data.size() - i;
    if (rest == 1)
    {
        unsigned int v = static_cast<unsigned int>(data[i]) << 16;
        out += ALPHABET[(v >> 18) & 0x3F];
        out += ALPHABET[(v >> 12) & 0x3F];
        out += "==";
    }
    else
    if (rest == 2)
    {
        unsigned int v = (static_cast<unsigned int>(data[i]) << 16)
                       | (static_cast<unsigned int>(data[i+1]) << 8);
        out += ALPHABET[(v >> 18) & 0x3F];
        out += ALPHABET[(v >> 12) & 0x3F];
        out += ALPHABET[(v >>  6) & 0x3F];
        out += '=';
    }

    return out;
}

} // namespace mcp
