// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Qt side of the MCP server: marshals to the GUI thread, source

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include "globals.h"
#include "mainwindow.h"
#include "mcp/mcp_server.h"
#include "mcp_bridge.h"

McpBridge::McpBridge(MainWindow * window, bool trace):
      QObject(window)
    , m_window(window)
    , m_trace(trace)
{
}

McpBridge::~McpBridge()
{
    //The reader thread is blocked in a read on stdin and there is no portable
    //way to interrupt that, so it is never joined: the process is going away
#if USE_QT_THREADING
    //EmuThread is heap allocated and outlives us on purpose
#else
    if (m_thread.joinable()) m_thread.detach();
#endif
}

void McpBridge::start()
{
    m_session.reset(new mcp::McpSession(m_window->e, this));
    m_server.reset(new mcp::McpServer(m_session.get(), PROJECT_VERSION, m_trace));

    mcp::McpServer * server = m_server.get();
    McpBridge * self = this;

#if USE_QT_THREADING
    m_thread = EmuThread::create([server, self]() {
        server->run();
        self->quit(0);
    });
#else
    m_thread = std::thread([server, self]() {
        server->run();
        self->quit(0);
    });
#endif
}

QString McpBridge::call_gui(const char * method, const QString &argument, bool has_argument)
{
    QString reply;

    //Blocking a queued call onto our own thread would deadlock
    const Qt::ConnectionType type = (QThread::currentThread() == thread())
        ? Qt::DirectConnection
        : Qt::BlockingQueuedConnection;

    if (has_argument)
        QMetaObject::invokeMethod(this, method, type,
                                  Q_RETURN_ARG(QString, reply), Q_ARG(QString, argument));
    else
        QMetaObject::invokeMethod(this, method, type, Q_RETURN_ARG(QString, reply));

    return reply;
}

//------------------------------- McpHost ----------------------------------//

std::string McpBridge::load_machine(const std::string &config)
{
    return call_gui("gui_load_machine", QString::fromStdString(config), true).toStdString();
}

std::string McpBridge::current_machine()
{
    return call_gui("gui_current_machine", QString(), false).toStdString();
}

void McpBridge::show_window()
{
    QMetaObject::invokeMethod(this, "gui_show_window",
        (QThread::currentThread() == thread()) ? Qt::DirectConnection : Qt::BlockingQueuedConnection);
}

void McpBridge::quit(int code)
{
    //Queued and not blocking: the caller is the reader thread on its way out
    QMetaObject::invokeMethod(this, "gui_quit", Qt::QueuedConnection, Q_ARG(int, code));
}

//--------------------------- GUI thread side ------------------------------//

QString McpBridge::gui_load_machine(const QString &config)
{
    if (m_window == nullptr) return QStringLiteral("The main window is gone.");
    return m_window->mcp_load_config(config);
}

QString McpBridge::gui_current_machine()
{
    if (m_window == nullptr) return QString();
    return m_window->mcp_current_machine();
}

void McpBridge::gui_show_window()
{
    if (m_window != nullptr) m_window->mcp_show_window();
}

void McpBridge::gui_quit(int code)
{
    qApp->exit(code);
}
