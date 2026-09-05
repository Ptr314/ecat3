// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: MCP server: JSON-RPC 2.0 over stdin/stdout, header

#pragma once

#include <string>

#include "emulator/thread_compat.h"

namespace mcp {

class McpSession;

// Speaks the Model Context Protocol over the standard streams. The stdio
// transport frames messages as one JSON object per line, so stdout carries
// nothing but JSON: every diagnostic goes to stderr.
//
// run() blocks until stdin reaches end of file, which is how the process
// learns that the client is gone. In the windowed build it runs on a thread of
// its own, in the console build on the main thread.
class McpServer
{
public:
    McpServer(McpSession * session, const std::string &version, bool trace);

    void run();
    //True once stdin has closed, so the frontend can quit
    bool is_finished() const { return m_finished; }

private:
    McpSession * m_session;
    std::string  m_version;
    bool         m_trace;
    bool         m_finished;
    compat_mutex m_out_mutex;

    void handle_line(const std::string &line);
    void send(const std::string &json);
    void trace(const char * direction, const std::string &json);
};

} // namespace mcp
