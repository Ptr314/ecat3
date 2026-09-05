// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Qt side of the MCP server: marshals to the GUI thread, header

#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "emulator/thread_compat.h"
#include "mcp/mcp_session.h"

class MainWindow;

namespace mcp { class McpServer; }

// The only Qt file of the MCP feature.
//
// Script commands are executed on the reader thread: submit() takes the engine
// mutex and the wait polls atomics, so nothing there touches Qt, and running a
// WAIT through the GUI thread would freeze the interface for the whole delay.
// Loading a machine is the opposite case - it rebuilds the device manager, the
// menus and the toolbars - so it is marshalled to the GUI thread and blocks.
class McpBridge : public QObject, public mcp::McpHost
{
    Q_OBJECT

public:
    McpBridge(MainWindow * window, bool trace);
    ~McpBridge() override;

    //Creates the session and starts the reader thread
    void start();

    //McpHost, all called from the reader thread
    std::string load_machine(const std::string &config) override;
    void        quit(int code) override;
    void        show_window() override;
    std::string current_machine() override;

    Q_INVOKABLE QString gui_load_machine(const QString &config);
    Q_INVOKABLE QString gui_current_machine();
    Q_INVOKABLE void    gui_show_window();
    Q_INVOKABLE void    gui_quit(int code);

private:
    MainWindow * m_window;
    bool         m_trace;

    std::unique_ptr<mcp::McpSession> m_session;
    std::unique_ptr<mcp::McpServer>  m_server;

#if USE_QT_THREADING
    EmuThread * m_thread = nullptr;
#else
    std::thread m_thread;
#endif

    //Runs the given invokable on the GUI thread, blocking until it is done
    QString call_gui(const char * method, const QString &argument, bool has_argument);
};
