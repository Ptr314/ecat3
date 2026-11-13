// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: main.cpp

#include "mainwindow.h"

#include <cstring>
#include <iostream>
#include <vector>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>

#include <QMessageBox>
#ifdef RENDERER_OPENGL
#include <QSurfaceFormat>
#endif

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    #include <SDL.h>
#endif

//The core reports through Result, but it still throws in places - a device
//looked up by a name the configuration does not have, a std::stoi on a value
//typed by hand - and an exception leaving a slot ends the process without a
//word. Caught here it becomes a message. Qt does not promise a consistent
//state after an exception has passed through its frames, so this is the last
//line, not a way of handling errors: the known throwers are fixed where they
//are, and what lands here is a bug to report
class EcatApplication : public QApplication
{
public:
    EcatApplication(int &argc, char **argv) : QApplication(argc, argv) {}

    bool notify(QObject * receiver, QEvent * event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &ex) {
            report(ex.what());
        } catch (...) {
            report("unknown exception");
        }
        return false;
    }

private:
    bool m_reporting = false;

    void report(const char * what)
    {
        std::cerr << "Unhandled exception: " << what << std::endl;
        //A timer that throws on every tick must not pile up message boxes
        if (m_reporting) return;
        m_reporting = true;
        QMessageBox::critical(nullptr, "eCat3",
            QCoreApplication::translate("main", "Internal error, the program may be unstable:") + "\n" + what);
        m_reporting = false;
    }
};

int main(int argc, char *argv[])
{
    #if defined(__linux__)
        qputenv("QT_QPA_PLATFORM", "xcb"); // Enforce using X11 on Linux
    #endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

#if defined(RENDERER_OPENGL) && (defined(_WIN32) || defined(__APPLE__))
    //No wait for the monitor's retrace when the frame is handed over: there
    //the desktop composes every window and syncs it to the screen anyway, so
    //the wait added only lag - 12 ms on average, 23 in one case of ten, with
    //the window's thread held all that time and the mouse portions and the
    //next frame waiting behind it. X11 without a compositor would tear
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);
#endif

    //A -style of the command line wins over the one saved in the ini. Qt takes
    //it out of the arguments, so it is looked for before Qt sees them
    bool style_given = qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE");
    for (int i = 1; i < argc; i++) {
        const QByteArray arg(argv[i]);
        if (arg == "-style" || arg == "--style" || arg.startsWith("-style=") || arg.startsWith("--style="))
            style_given = true;
    }

    //--platform of a program launch is an option of Qt itself as well:
    //-platform picks the plugin of the window system, and Qt takes it with two
    //dashes too. "БК0010" would be a plugin it cannot find, and the process
    //would end right there. So --platform <name> is taken out of the arguments
    //before Qt sees them; the plugin is still chosen by -platform with one
    //dash or by QT_QPA_PLATFORM. --platform=<name> Qt leaves alone. The value
    //is kept for the case QCommandLineParser no longer sees it: on Windows the
    //parser reads the wide command line, and does see it, unless Qt took some
    //other option of its own out (-style)
    QString platform_arg;
    std::vector<char*> qt_argv;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && i + 1 < argc && std::strcmp(argv[i], "--platform") == 0) {
            platform_arg = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        qt_argv.push_back(argv[i]);
    }
    int qt_argc = static_cast<int>(qt_argv.size());
    qt_argv.push_back(nullptr);

    EcatApplication a(qt_argc, qt_argv.data());
    a.setProperty("ecat_style_given", style_given);

    //Qt plays the "fade or slide menus into view" of Windows itself, on the
    //window's thread: every menu and submenu took 150 ms of changing
    //opacity, side by side with the frames of the machine. Shown at once
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);

    QCoreApplication::setApplicationName("eCat3");
    QCoreApplication::setApplicationVersion(PROJECT_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "eCat3, a universal emulator of retro computers"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOption(QStringList() << "c" << "config",
        QCoreApplication::translate("main", "Machine configuration to load, overrides the one saved in the ini file."),
        QCoreApplication::translate("main", "file.cfg"));
    parser.addOption(configOption);

    QCommandLineOption scriptOption(QStringList() << "s" << "script",
        QCoreApplication::translate("main", "Script to run, see docs/SCRIPTING.md."),
        QCoreApplication::translate("main", "file.ecat"));
    parser.addOption(scriptOption);

    QCommandLineOption platformOption(QStringList() << "platform",
        QCoreApplication::translate("main", "Run the program given as the file on the machine of this platform (bk10, bk11m...), see launch/platforms.cfg."),
        QCoreApplication::translate("main", "name"));
    parser.addOption(platformOption);

    parser.addPositionalArgument(QCoreApplication::translate("main", "file"),
        QCoreApplication::translate("main", "A .cfg configuration, a .ecat script, or the program for --platform."));

    //Registered whatever the build: an MCP client launches the executable
    //with --mcp, and a build without the server has to say so on stderr and
    //exit rather than pop up a modal "unknown option" box that nobody sees
    QCommandLineOption mcpOption(QStringList() << "mcp",
        QCoreApplication::translate("main", "Act as an MCP server on stdin/stdout, needs a build with ENABLE_MCP, see docs/MCP.md."));
    parser.addOption(mcpOption);

    QCommandLineOption mcpTraceOption(QStringList() << "mcp-trace",
        QCoreApplication::translate("main", "Print the MCP conversation to stderr."));
    parser.addOption(mcpTraceOption);

    QCommandLineOption workdirOption(QStringList() << "workdir",
        QCoreApplication::translate("main", "Directory to work in, normally the one holding computers/."),
        QCoreApplication::translate("main", "directory"));
    parser.addOption(workdirOption);

    QCommandLineOption speedOption(QStringList() << "speed",
        QCoreApplication::translate("main", "Speed of emulation for this run: 0.25 to 1000, or max, see Emulation > Speed."),
        QCoreApplication::translate("main", "factor"));
    parser.addOption(speedOption);

    parser.process(a);

    int speed_q = -1;
    if (parser.isSet(speedOption))
    {
        speed_q = Emulator::parse_speed(parser.value(speedOption).toStdString());
        if (speed_q < 0)
        {
            std::cerr << "Invalid speed: " << parser.value(speedOption).toStdString()
                      << " (0.25 to 1000 in steps of 0.25, or max)" << std::endl;
            return 2;
        }
    }

    const bool mcp_mode  = parser.isSet(mcpOption);
    const bool mcp_trace = parser.isSet(mcpTraceOption);

#ifndef ENABLE_MCP
    if (mcp_mode)
    {
        std::cerr << "This build has no MCP server. Rebuild with -DENABLE_MCP=ON, see docs/MCP.md."
                  << std::endl;
        return 2;
    }
#endif

    //A program is named relative to the directory the user is in, which
    //--workdir is about to leave
    const QDir start_dir = QDir::current();

    //Set before the main window is built: it resolves the emulator root and
    //the ini file against the current directory
    const QString workdir = parser.value(workdirOption);
    if (!workdir.isEmpty() && !QDir::setCurrent(workdir))
    {
        std::cerr << "Unable to change the working directory to "
                  << workdir.toStdString() << std::endl;
        return 2;
    }

    QString config_file = parser.value(configOption);
    QString script_file = parser.value(scriptOption);

    //Positional arguments are dispatched by their extension, so that a script
    //or a configuration can simply be dropped onto the executable. Any other
    //file is a program for --platform to run
    QString program;
    const QStringList positional = parser.positionalArguments();
    for (const QString &arg : positional)
    {
        QString suffix = QFileInfo(arg).suffix().toLower();
        if (suffix == "ecat") {
            if (script_file.isEmpty()) script_file = arg;
        } else if (is_machine_file(arg.toStdString())) {
            //.cfg, or a configuration extension (.ext, .ext.zip)
            if (config_file.isEmpty()) config_file = arg;
        } else if (program.isEmpty()) {
            program = start_dir.absoluteFilePath(arg);
        }
    }

    //The launch script of the platform is the script of the run and names the
    //machine itself
    const QString platform = parser.isSet(platformOption) ? parser.value(platformOption) : platform_arg;
    if (!platform.isEmpty() && (program.isEmpty() || !script_file.isEmpty() || !config_file.isEmpty() || mcp_mode))
    {
        std::cerr << (program.isEmpty()
                      ? "--platform needs the file of a program to run"
                      : "--platform cannot be combined with --script, --config or --mcp")
                  << std::endl;
        return 2;
    }

#ifdef RENDERER_SDL2
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#elif defined(USE_SDL_AUDIO)
    SDL_Init(SDL_INIT_AUDIO);
#endif

    MainWindow w(config_file, script_file, speed_q);
    w.setWindowIcon(QIcon(":/icons/tv"));
    if (!platform.isEmpty()) w.set_platform_launch(platform, program);

#ifdef ENABLE_MCP
    if (mcp_mode)
    {
        //No show(): the window appears when the client asks for a machine, so
        //that a session that never touches the emulator costs nothing visible
        w.enable_mcp(mcp_trace);
    }
    else
#else
    (void)mcp_trace;
#endif
    w.show();

    int RetVal = a.exec();

    //Closing the window ends exec() with zero whatever the script asked for,
    //so the EXIT code of a command line script is applied here
    if (RetVal == 0) RetVal = w.script_exit_code();

#if defined(RENDERER_SDL2) || defined(USE_SDL_AUDIO)
    SDL_Quit();
#endif

    return RetVal;
}
