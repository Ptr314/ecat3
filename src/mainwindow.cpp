// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main window source

#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QEvent>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QFile>
#include <QFileDialog>
#include <QWidgetAction>
#include <QPushButton>
#include <QActionGroup>
#include <QSignalBlocker>
#include <QBoxLayout>
// #include <QOverload>
#include <QMessageBox>
#include <cstdlib>
#include <cstring>
#include <QCoreApplication>
#include <QFileInfo>
#include <iostream>
#include <QCursor>
#include <QMouseEvent>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QProgressDialog>
//Qt 5.6 не тянет QDateTime за собой: там, где он нужен, его надо позвать
#include <QDateTime>

#include "dialogs/genericdbgwnd.h"
#include "dialogs/i8255window.h"
#include "mainwindow.h"
#include "emulator/utils.h"
#include "emulator/cache.h"
#include "dsk_tools/core.h"
#include "qt_utils.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"
#include "qevent.h"
#include "ui_mainwindow.h"
#include "dialogs/ui_aboutdlg.h"
#include "emulator/debug.h"
#include "emulator/files.h"
#include "dialogs/dumpwindow.h"
#include "dialogs/mmwindow.h"
#include "dialogs/debugwindow.h"
#include "dialogs/portwindow.h"
#include "dialogs/openconfigwindow.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/hdd_image.h"
#include "emulator/devices/common/tape.h"
#include "emulator/script/script_parser.h"
#include "dialogs/taperecorder.h"
#include "dialogs/keyboardwindow.h"

#include "libs/lodepng/lodepng.h"

#ifdef ENABLE_MCP
    #include "mcp_bridge.h"
#endif

#ifdef RENDERER_SDL2
    #include "renderers/renderer_sdl2.h"
#elif defined(RENDERER_QT)
    #include "renderers/renderer_qt.h"
#elif defined(RENDERER_OPENGL)
    #include "renderers/GLWidget.h"
    #include "renderers/renderer_opengl.h"
#endif

MainWindow::MainWindow(const QString &config_file, const QString &script_file, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fdd_timer(nullptr)
    , fdds_found(0)
    , fdd_blinker(false)
    , cmdline_config(config_file)
    , script_file(script_file)
    //, fdc(nullptr)
{
    QFontDatabase::addApplicationFont(":/fonts/mono-bold");
    QFontDatabase::addApplicationFont(":/fonts/mono-regular");
    QFontDatabase::addApplicationFont(":/fonts/mono-semibold");
    QFontDatabase::addApplicationFont(":/fonts/consolas");
    QFontDatabase::addApplicationFont(":/fonts/dos");

    QString app_path = QApplication::applicationDirPath();
    QString current_path = QDir::currentPath();
    QString work_path, software_path, data_path, emulator_root, ini_path, ini_file;

    //An ini that is not there yet is made a copy of the first source found.
    //deploy/ecat.ini is the developer's own file and is not in git, so a fresh
    //checkout carries only the distributed defaults, deploy/.ecat.ini - and
    //without their [TapeFiles] no tape image loads. QFile::copy() never
    //overwrites and never throws, unlike the std::filesystem call it replaces
    auto seed_ini = [](const QString &target, const QStringList &sources) {
        if (QFileInfo::exists(target)) return;
        for (const QString &s : sources)
            if (QFileInfo::exists(s)) {
                QFile::copy(s, target);
                return;
            }
    };

#if defined(__linux__)
    if (std::filesystem::exists(QString(current_path + "/computers").toStdString())) {
        emulator_root = current_path;
    } else {
        emulator_root = app_path.left(app_path.lastIndexOf('/')) + "/share/ecat";
    }

    ini_path = QString(getenv("HOME")) + "/.config";
    ini_file = ini_path + "/ecat.ini";
    seed_ini(ini_file, {emulator_root + "/ecat.ini", current_path + "/ecat.ini", emulator_root + "/.ecat.ini"});
#elif defined(__APPLE__)
    if (std::filesystem::exists(QString(current_path + "/computers").toStdString())) {
        emulator_root = current_path;
    } else {
        emulator_root = app_path.left(app_path.lastIndexOf('/')) + "/Resources";
    }

    ini_path = QString(getenv("HOME"));
    ini_file = ini_path + "/.ecat.ini";
    seed_ini(ini_file, {emulator_root + "/ecat.ini", current_path + "/ecat.ini", emulator_root + "/.ecat.ini"});
#elif defined(_WIN32)
    QFileInfo ini_fi(app_path + "/ecat.ini");
    if (ini_fi.exists() && ini_fi.isFile()) {
        ini_path = app_path;
    } else {
        ini_path = current_path;
    }
    ini_file = ini_path + "/ecat.ini";

    QFileInfo comp_fi(current_path + "/computers");
    if (comp_fi.exists() && !comp_fi.isFile()) {
        emulator_root = current_path;
    } else {
        emulator_root = app_path;
    }
    seed_ini(ini_file, {emulator_root + "/.ecat.ini"});
#else
#error "Unknown platform"
#endif

    work_path = emulator_root + "/computers/";
    software_path = emulator_root + "/software/";
    data_path = emulator_root + "/data/";

    m_settings = make_unique<IniSettings>(ini_file.toStdString());
    QString ini_lang = QString::fromStdString(m_settings->get("interface", "language"));

    if (ini_lang.length() == 0) {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString &locale : uiLanguages) {
            const QString baseName = QLocale(locale).name().toLower();
            if (baseName.startsWith("en_"))
                continue;
            if (switch_language(baseName, true))
                break;
        }
    } else {
        switch_language(ini_lang, true);
    }

    ui->setupUi(this);

    #ifdef RENDERER_SDL2
        screen = new QLabel(this);
        setCentralWidget(screen);
        screen->setUpdatesEnabled(false);
    #elif defined(RENDERER_QT)
        screen = new QtRenderWidget(this);
        setCentralWidget(screen);
        dynamic_cast<QtRenderWidget*>(screen)->setAlignment(Qt::AlignCenter);
        screen->setStyleSheet("background-color: black;");
    #elif defined(RENDERER_OPENGL)
        screen = new GLWidget(this);
        setCentralWidget(screen);
    #endif

    //The mouse of the machine takes the host mouse from the screen widget
    screen->installEventFilter(this);
    mouse_timer = new QTimer(this);
    connect(mouse_timer, &QTimer::timeout, this, [this]() { mouse_flush(false); });

    add_languages();

#ifdef SDL_SEPARATE_WINDOW
    resize(500,100);
#endif

    //The processor button is drawn at three quarters of the rest of the bar.
    //A tool button whose parent is the tool bar takes the icon size of the bar
    //(QToolButton::initStyleOption), so it lives in a holder of its own, the
    //same way the recording panel below does. The action keeps its place in
    //the bar: the holder goes in front of it, then the action itself leaves
    QWidget * cpu_holder = new QWidget(this);
    QBoxLayout * cpu_layout = new QBoxLayout(QBoxLayout::LeftToRight, cpu_holder);
    cpu_layout->setContentsMargins(0, 0, 0, 0);
    cpu_layout->setSpacing(0);

    QToolButton * cpu_button = new QToolButton(cpu_holder);
    cpu_button->setDefaultAction(ui->actionCPUState);
    cpu_button->setIconSize(ui->toolBar->iconSize() * 3 / 4);
    cpu_button->setAutoRaise(true);
    cpu_button->setFocusPolicy(Qt::NoFocus);
    cpu_layout->addWidget(cpu_button);

    ui->toolBar->insertWidget(ui->actionCPUState, cpu_holder);
    ui->toolBar->removeAction(ui->actionCPUState);

    //The recording controls sit at the far end of the tool bar, after a
    //stretch. UpdateToolbar() inserts everything before actionDebugger, so a
    //machine change never touches them. They are tool buttons of their own
    //rather than plain actions, so that they can be drawn smaller than the
    //rest of the bar; each follows its action (icon, text, enabled state)
    QWidget * rec_spacer = new QWidget(this);
    rec_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->toolBar->addWidget(rec_spacer);
    rec_panel_separator = ui->toolBar->addSeparator();

    //A tool button whose parent is the tool bar takes the icon size of the
    //bar (QToolButton::initStyleOption), so the buttons live in a holder of
    //their own. The holder follows the orientation of the bar
    QWidget * rec_holder = new QWidget(this);
    QBoxLayout * rec_layout = new QBoxLayout(
        (ui->toolBar->orientation() == Qt::Vertical)?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight, rec_holder);
    rec_layout->setContentsMargins(0, 0, 0, 0);
    rec_layout->setSpacing(0);
    connect(ui->toolBar, &QToolBar::orientationChanged, rec_layout, [rec_layout](Qt::Orientation o) {
        rec_layout->setDirection((o == Qt::Vertical)?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
    });

    const QSize rec_icon_size = ui->toolBar->iconSize() * 3 / 5;   //60% of the bar
    QAction * rec_actions[] = {ui->actionRecOpen, ui->actionRecSave, ui->actionRecord,
                               ui->actionRecPlay, ui->actionRecRewind, ui->actionRecStop};
    for (size_t i = 0; i < sizeof(rec_actions) / sizeof(rec_actions[0]); i++) {
        QToolButton * button = new QToolButton(rec_holder);
        button->setDefaultAction(rec_actions[i]);
        button->setIconSize(rec_icon_size);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        rec_layout->addWidget(button);
    }
    rec_panel_widget = ui->toolBar->addWidget(rec_holder);

    //Recording position in the status bar, left of the sound controls.
    //Hidden until the first recording or opened file
    rec_label = new QLabel(this);
    rec_label->setContentsMargins(4, 0, 8, 0);
    rec_label->hide();
    statusBar()->addPermanentWidget(rec_label, 0);

    QIcon icon;
    icon.addFile(QString::fromUtf8(":/icons/sound"), QSize(), QIcon::Normal, QIcon::Off);
    icon.addFile(QString::fromUtf8(":/icons/sound-mute"), QSize(), QIcon::Normal, QIcon::On);

    mute = new QToolButton(this);
    mute->setFocusPolicy(Qt::NoFocus);
    mute->setCheckable(true);
    mute->setIcon(icon);
    mute->setStyleSheet(
                            "QToolButton { /* all types of tool button */"
                            "border: 1px solid #8f8f91;"
                            "border-radius: 2px;"
                            "}"
        );
    statusBar()->addPermanentWidget(mute, 0);

    connect(mute, &QToolButton::toggled, this, &MainWindow::set_mute);  // No QOverload needed: toggled(bool) is not overloaded


    volume = new QSlider(Qt::Horizontal, this);
    volume->setStyleSheet(
                            "QSlider::groove:horizontal {"
                            "border: 1px solid #999999;"
                            "height: 4px; /* the groove expands to the size of the slider by default. by giving it a height, it has a fixed size */"
                            "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #B1B1B1, stop:1 #c4c4c4);"
                            "margin: 2px 0;"
                            "}"
                            "QSlider::handle:horizontal {"
                            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #b4b4b4, stop:1 #8f8f8f);"
                            "border: 1px solid #5c5c5c;"
                            "width: 8px;"
                            "margin: -2px 0; /* handle is placed by default on the contents rect of the groove. Expand outside the groove */"
                            "    border-radius: 3px;"
                            "}"
//                            "QSlider::add-page:horizontal {"
//                            "background: white;"
//                            "}"
                            "QSlider::sub-page:horizontal {"
                            "background: #00FF00;"
                            "margin: 3px 1px;"
                            "}"
        );
    volume->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    volume->setFocusPolicy(Qt::NoFocus);
    volume->setMinimumWidth(100);
    volume->setMinimum(0);
    volume->setMaximum(100);
    //volume->setValue(50);
    statusBar()->addPermanentWidget(volume, 0);

    connect(volume, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), this, &MainWindow::set_volume);

    DWM = new DebugWindowsManager();

    DWM->register_debug_window("rom", &CreateDumpWindow);
    DWM->register_debug_window("ram", &CreateDumpWindow);
    DWM->register_debug_window("memory-mapper", &CreateMMWindow);
    DWM->register_debug_window("i8080", &CreateDebugWindow);
    DWM->register_debug_window("port", &CreatePortWindow);
    DWM->register_debug_window("port-address", &CreatePortWindow);
    DWM->register_debug_window("i8255", &CreateI8255Window);
    DWM->register_debug_window("z80", &CreateDebugWindow);
    DWM->register_debug_window("6502", &CreateDebugWindow);
    DWM->register_debug_window("65c02", &CreateDebugWindow);
    DWM->register_debug_window("1801vm1", &CreateDebugWindow);
    DWM->register_debug_window("1801vm2", &CreateDebugWindow);
    DWM->register_debug_window("taperecorder", &CreateTapeWindow);
    DWM->register_debug_window("ram-address", &CreateDumpWindow);
    DWM->register_debug_window("register", &CreatePortWindow);

    #ifdef RENDERER_SDL2
        renderer = new SDL2Renderer();
    #elif defined(RENDERER_QT)
        renderer = new QtRenderer();
    #elif defined(RENDERER_OPENGL)
        renderer = new OpenGLRenderer();
    #endif
    e = new Emulator(work_path.toStdString(), data_path.toStdString(), software_path.toStdString(), ini_file.toStdString(), renderer);
    e->user_ext_path = default_user_ext_path(emulator_root.toStdString());
    e->cache_path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).absoluteFilePath("ecat3-cache").toStdString() + "/";
    //Unpacked archives and inline data nobody has used for a month
    cache_cleanup(e->cache_path, CACHE_MAX_AGE_DAYS);

    // Signal/slot connections removed — using direct calls to Emulator methods

    QString file_to_load = QString::fromStdString(e->read_setup("Startup", "default", ""));

    last_path = QString::fromStdString(e->get_last_path());

    QString sound_volume = QString::fromStdString(e->read_setup("Sound", "volume", "50"));
    volume->setValue(sound_volume.toInt());

    QString muted = QString::fromStdString(e->read_setup("Sound", "muted", "0"));
    mute->setChecked(muted.toInt() == 1);

    //The recording block of the tool bar can be hidden from the Display menu
    ui->actionRecPanel->setChecked(e->read_setup("Video", "recording_panel", "1") != "0");
    ui->actionRememberDisks->setChecked(e->read_setup("Core", "remember_disks", "0") == "1");

    //Size, position and the maximized state of the previous run. Qt moves a
    //window saved on a monitor that is gone back onto an existing one
    std::string geometry = e->read_setup("Window", "geometry", "");
    if (!geometry.empty())
        restoreGeometry(QByteArray::fromHex(QByteArray::fromStdString(geometry)));

    //A configuration given on the command line wins over the one saved in the ini
    first_config = cmdline_config.isEmpty()
        ? (QFileInfo(file_to_load).isAbsolute() ? file_to_load : work_path + file_to_load)
        : resolve_startup_path(cmdline_config);

    //The engine runs on the emulation thread, so its progress is picked up
    //here by polling rather than by a cross thread call
    rec_timer = new QTimer(this);
    connect(rec_timer, &QTimer::timeout, this, &MainWindow::rec_tick);
    rec_timer->start(100);
}

QString MainWindow::resolve_startup_path(const QString &file_name) const
{
    if (file_name.isEmpty()) return file_name;

    //An absolute name, or one that resolves against the current directory,
    //is taken as is. Everything else is relative to computers/, the same way
    //the [Startup] default entry of the ini file is treated.
    if (QFileInfo(file_name).isAbsolute()) return file_name;
    if (QFileInfo::exists(file_name)) return QFileInfo(file_name).absoluteFilePath();

    return QString::fromStdString(e->work_path) + file_name;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (first_show) {
        // We use this trick to ensure that all interface elements already have their final dimensions (especially on Linux).
        first_show = false;

#ifdef ENABLE_MCP
        //The machine comes from the client, not from the ini file: loading the
        //default one first would cost a second machine bring-up every session
        if (mcp_mode) return;
#endif

        //The script is parsed before the machine is loaded: its MACHINE command
        //may name the configuration to start with
        if (!script_file.isEmpty())
        {
            QString path = QFileInfo(script_file).exists()
                ? QFileInfo(script_file).absoluteFilePath()
                : script_file;

            emulator::Result res = e->load_script(path.toStdString());
            if (!res) {
                QMessageBox::warning(this, tr("Error"), translateResultMessage(res.message));
                script_file.clear();
            } else {
                std::string machine = e->script_machine();
                if (cmdline_config.isEmpty() && !machine.empty())
                    first_config = resolve_startup_path(QString::fromStdString(machine));
            }
        }

        startup_load = !cmdline_config.isEmpty();
        cmdline_starting = !script_file.isEmpty();
        load_config(first_config, false, script_file.isEmpty());
        cmdline_starting = false;
        startup_load = false;
        CreateScreenMenu();

        if (!script_file.isEmpty()) start_script();
    }
}

void MainWindow::start_script(bool from_cmdline)
{
    if (!e->loaded) return;

    //A script from the command line runs through the same controls as a
    //recording, the only difference being that its EXIT closes the window
    e->start_script();
    rec_cmdline = from_cmdline;
    rec_ui_shown = true;
    rec_state = RecPlaying;
    rec_seen_pc = 0;
    rec_refresh_total();
    rec_update_ui();
}

bool MainWindow::switch_language(const QString & lang, bool init)
{
    //The sources are in English, so en_us.qm holds no messages, and Qt 5.6
    //refuses to load a file without them (later versions accept it). A file
    //that is there but empty is the source language: nothing to install
    const QString file = ":/i18n/" + lang;
    const bool loaded = translator.load(file);
    if (loaded || QFile::exists(file)) {
        qApp->removeTranslator(&translator);
        qApp->removeTranslator(&qtTranslator);
        if (loaded) qApp->installTranslator(&translator);

        QString t = QString(":/i18n/qtbase_%1.qm").arg(lang.split("_")[0]);
        if (qtTranslator.load(t)) {
            qApp->installTranslator(&qtTranslator);
        }
        if (!init) {
            ui->retranslateUi(this);
            CreateScreenMenu();
            rec_update_ui();
            //Through the emulator's copy of the ini: each copy writes out the
            //whole file as it holds it, and this one was read before the
            //emulator wrote anything (options, disks), which a save from here
            //would drop - just as the emulator's next save would drop the
            //language written here
            if (e != nullptr) {
                e->write_setup("interface", "language", lang.toStdString());
            } else {
                m_settings->set("interface", "language", lang.toStdString());
                m_settings->save();
            }
        }
        return true;
    } else {
        if (!init) {
            QMessageBox::warning(this, MainWindow::tr("Error"), MainWindow::tr("Failed to load language file for: ") + lang);
        }
        return false;
    }
}

void MainWindow::add_languages()
{
    QAction *langsAction = ui->actionLanguage;

    QMenu *subMenu = new QMenu(MainWindow::tr("Languages"), this);

    QAction *subAction1 = subMenu->addAction(QIcon(":/icons/ru"), MainWindow::tr("Русский"));
    connect(subAction1, &QAction::triggered, this, [this]() { switch_language("ru_ru", false); });

    QAction *subAction2 = subMenu->addAction(QIcon(":/icons/en"), MainWindow::tr("English"));
    connect(subAction2, &QAction::triggered, this, [this]() { switch_language("en_us", false); });

    langsAction->setMenu(subMenu);
}

//What the tooltip of a drive button says: the name the configuration gives
//the drive ("title": MY0, A:), or the fallback numbering the drives
static QString drive_title(ComputerDevice * d, const QString &fallback)
{
    const std::string title = str_trim(d->config_parameter("title"));
    return title.empty() ? fallback : QString::fromStdString(title);
}

void MainWindow::CreateFDDMenu(unsigned int n)
{
    fdd_menu[n] = new QMenu(this);
    QAction * a1 = new QAction(MainWindow::tr("<Not loaded>"), this);
    a1->setIcon(QIcon(":/icons/cdrom_unmount"));
    a1->setEnabled(false);
    QAction * a2 = new QAction(QString(MainWindow::tr("Open an image...")), this);
    a2->setIcon(QIcon(":/icons/open"));
    connect(a2, &QAction::triggered, this, [this, n](){fdd_open(n);});
    QAction * a3 = new QAction(QString(MainWindow::tr("Write protect")), this);
    connect(a3, &QAction::triggered, this, [this, n](){fdd_wp(n);});
    a3->setIcon(QIcon(":/icons/lock"));
    QAction * a4 = new QAction(QString(MainWindow::tr("Eject")), this);
    connect(a4, &QAction::triggered, this, [this, n](){fdd_eject(n);});
    a4->setIcon(QIcon(":/icons/eject"));
    QAction * a5 = new QAction(QString(MainWindow::tr("Write to a file...")), this);
    connect(a5, &QAction::triggered, this, [this, n](){fdd_write(n);});
    a5->setIcon(QIcon(":/icons/file_save"));
    QAction * a6 = new QAction(QString(MainWindow::tr("Default image")), this);
    connect(a6, &QAction::triggered, this, [this, n](){fdd_default(n);});
    fdd_menu[n]->addAction(a1);
    fdd_menu[n]->addSeparator();
    fdd_menu[n]->addAction(a2);
    fdd_menu[n]->addAction(a3);
    fdd_menu[n]->addAction(a4);
    fdd_menu[n]->addAction(a6);
    fdd_menu[n]->addAction(a5);
    //Available whenever something is remembered, the setting on or off: it is
    //also how an entry is got rid of
    connect(fdd_menu[n], &QMenu::aboutToShow, this, [this, n, a6](){
        a6->setEnabled(n < fdds.size() && e->has_remembered_disk(fdds[n]->name));
    });

    fdd_button[n] = new QToolButton();
    fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    fdd_button[n]->setMenu(fdd_menu[n]);
    fdd_button[n]->setPopupMode(QToolButton::MenuButtonPopup);
    fdd_button[n]->setFocusPolicy(Qt::NoFocus);
    fdd_button[n]->setToolTip(drive_title(fdds[n], MainWindow::tr("Floppy drive %1").arg(n + 1)));

    connect(fdd_button[n], &QToolButton::clicked, this, [this, n](){fdd_open(n);});

    ui->toolBar->insertWidget(ui->actionDebugger, fdd_button[n] );
}

//Кнопка винчестера. В отличие от дисковода образ никуда не сохраняют: машина
//пишет прямо в файл, поэтому пунктов всего три - открыть, защитить, вынуть
void MainWindow::CreateHDDMenu(unsigned int n)
{
    hdd_menu[n] = new QMenu(this);
    QAction * a1 = new QAction(MainWindow::tr("<Not loaded>"), this);
    a1->setIcon(QIcon(":/icons/hdd_unmount"));
    a1->setEnabled(false);
    QAction * a2 = new QAction(QString(MainWindow::tr("Open an image...")), this);
    a2->setIcon(QIcon(":/icons/open"));
    connect(a2, &QAction::triggered, this, [this, n](){hdd_open(n);});
    QAction * a3 = new QAction(QString(MainWindow::tr("Write protect")), this);
    a3->setIcon(QIcon(":/icons/lock"));
    a3->setCheckable(true);
    connect(a3, &QAction::triggered, this, [this, n](){hdd_wp(n);});
    QAction * a4 = new QAction(QString(MainWindow::tr("Eject")), this);
    a4->setIcon(QIcon(":/icons/eject"));
    connect(a4, &QAction::triggered, this, [this, n](){hdd_eject(n);});
    QAction * a5 = new QAction(QString(MainWindow::tr("Default image")), this);
    connect(a5, &QAction::triggered, this, [this, n](){hdd_default(n);});
    hdd_menu[n]->addAction(a1);
    hdd_menu[n]->addSeparator();
    hdd_menu[n]->addAction(a2);
    hdd_menu[n]->addAction(a3);
    hdd_menu[n]->addAction(a4);
    hdd_menu[n]->addAction(a5);
    connect(hdd_menu[n], &QMenu::aboutToShow, this, [this, n, a5](){
        a5->setEnabled(n < hdds.size() && e->has_remembered_disk(hdds[n]->name));
    });

    hdd_button[n] = new QToolButton();
    hdd_button[n]->setIcon(QIcon(":/icons/hdd_unmount"));
    hdd_button[n]->setMenu(hdd_menu[n]);
    hdd_button[n]->setPopupMode(QToolButton::MenuButtonPopup);
    hdd_button[n]->setFocusPolicy(Qt::NoFocus);
    hdd_button[n]->setToolTip(drive_title(hdds[n], MainWindow::tr("Hard disk %1").arg(n + 1)));

    connect(hdd_button[n], &QToolButton::clicked, this, [this, n](){hdd_open(n);});

    ui->toolBar->insertWidget(ui->actionDebugger, hdd_button[n]);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::CreateScreenMenu()
{
    //Speed of the machine's mouse, a setting of the host rather than of a machine
    try {
        mouse_speed = std::stoi(e->read_setup("Mouse", "speed", "25"));
    } catch (const std::exception &) {
        mouse_speed = 25;
    }
    if (mouse_speed < 1 || mouse_speed > 400) mouse_speed = 25;
    if (mouse_speed_menu == nullptr) {
        //Settings, after the host settings and before About
        mouse_speed_menu = new QMenu(this);
        const QList<QAction*> acts = ui->menuHelp->actions();
        const int at = acts.indexOf(ui->actionRecPanel);
        ui->menuHelp->insertMenu((at >= 0 && at + 1 < acts.size())?acts[at + 1]:nullptr, mouse_speed_menu);
    }
    mouse_speed_menu->setTitle(tr("Mouse speed"));
    mouse_speed_menu->clear();
    QActionGroup * speed_group = new QActionGroup(mouse_speed_menu);
    static const int SPEEDS[] = {100, 50, 25, 12};
    for (size_t i = 0; i < sizeof(SPEEDS) / sizeof(SPEEDS[0]); i++) {
        const int speed = SPEEDS[i];
        QAction * sa = mouse_speed_menu->addAction(
            QString::number(speed) + "%",
            [this, speed]{
                mouse_speed = speed;
                e->write_setup("Mouse", "speed", std::to_string(speed));
            });
        sa->setActionGroup(speed_group);
        sa->setCheckable(true);
        sa->setChecked(mouse_speed == speed);
    }

    screenshot_as_shown = e->read_setup("Video", "screenshot", "original") == "shown";
    if (screenshot_menu == nullptr) {
        screenshot_menu = new QMenu(this);
        const QList<QAction*> acts = ui->menuHelp->actions();
        const int at = acts.indexOf(mouse_speed_menu->menuAction());
        ui->menuHelp->insertMenu((at >= 0 && at + 1 < acts.size())?acts[at + 1]:nullptr, screenshot_menu);
    }
    screenshot_menu->setTitle(tr("Screenshot type"));
    screenshot_menu->clear();
    QActionGroup * shot_group = new QActionGroup(screenshot_menu);
    for (int i = 0; i < 2; i++) {
        const bool shown = i == 1;
        QAction * sa = screenshot_menu->addAction(
            shown?tr("As on screen"):tr("Original"),
            [this, shown]{
                screenshot_as_shown = shown;
                e->write_setup("Video", "screenshot", shown?"shown":"original");
            });
        sa->setActionGroup(shot_group);
        sa->setCheckable(true);
        sa->setChecked(screenshot_as_shown == shown);
    }

    if (video_menu == nullptr) {
        video_menu = new QMenu(this);
        const QList<QAction*> acts = ui->menuHelp->actions();
        const int at = acts.indexOf(screenshot_menu->menuAction());
        ui->menuHelp->insertMenu((at >= 0 && at + 1 < acts.size())?acts[at + 1]:nullptr, video_menu);
    }
    video_menu->setTitle(tr("Video recording"));
    video_menu->clear();
    qDeleteAll(video_menu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
    {
        const int codec = VideoRecorder::codec_by_name(e->read_setup("Video", "video_codec", "h264"));
        QMenu * codec_menu = video_menu->addMenu(tr("Codec"));
        QActionGroup * codec_group = new QActionGroup(codec_menu);
        static const int CODECS[] = {VIDEO_CODEC_ZMBV, VIDEO_CODEC_H264, VIDEO_CODEC_H265, VIDEO_CODEC_PRORES};
        static const char * const CODEC_TITLES[] = {"ZMBV (AVI)", "H.264 (MP4)", "H.265 (MP4)", "ProRes (MOV)"};
        for (int i = 0; i < 4; i++) {
            const int c = CODECS[i];
            QAction * ca = codec_menu->addAction(CODEC_TITLES[i], [this, c]{
                e->write_setup("Video", "video_codec", VideoRecorder::codec_name(c));
            });
            ca->setActionGroup(codec_group);
            ca->setCheckable(true);
            ca->setChecked(codec == c);
        }

        const std::string fps_setting = e->read_setup("Video", "video_fps", "50");
        int fps = 50;
        try { fps = std::stoi(fps_setting); } catch (const std::exception &) {}
        QMenu * fps_menu = video_menu->addMenu(tr("Frame rate"));
        QActionGroup * fps_group = new QActionGroup(fps_menu);
        //Frame by frame with the machine where its display knows its rate
        //(48.828125 on the БК), 50 elsewhere
        QAction * native_action = fps_menu->addAction(tr("As the machine"), [this]{
            e->write_setup("Video", "video_fps", "native");
        });
        native_action->setActionGroup(fps_group);
        native_action->setCheckable(true);
        native_action->setChecked(fps_setting == "native");
        if (fps_setting == "native") fps = 0;
        static const int RATES[] = {25, 30, 50, 60};
        for (int i = 0; i < 4; i++) {
            const int r = RATES[i];
            QAction * fa = fps_menu->addAction(QString::number(r) + " " + tr("fps"), [this, r]{
                e->write_setup("Video", "video_fps", std::to_string(r));
            });
            fa->setActionGroup(fps_group);
            fa->setCheckable(true);
            fa->setChecked(fps == r);
        }

        video_menu->addSeparator();
        video_menu->addAction(tr("Folder for videos..."), [this]{
            QString dir = QString::fromStdString(e->read_setup("Video", "video_folder", ""));
            if (dir.isEmpty()) dir = QString::fromStdString(e->work_path) + "/video";
            dir = QFileDialog::getExistingDirectory(this, tr("Folder for videos"), dir);
            if (!dir.isEmpty()) e->write_setup("Video", "video_folder", QDir::toNativeSeparators(dir).toStdString());
        });
        video_menu->addAction(tr("Path to ffmpeg..."), [this]{
            const QString file = QFileDialog::getOpenFileName(this, tr("Path to ffmpeg"), find_ffmpeg());
            if (!file.isEmpty()) e->write_setup("Video", "ffmpeg", QDir::toNativeSeparators(file).toStdString());
        });
    }

    //Host keys moved onto the machine's keypad, for keyboards that lack one.
    //What a mode remaps is the machine's business: its keyboard names the tables
    static const char * const REMAP_NAMES[KEY_REMAP_COUNT] = {"none", "arrows", "letters"};
    const std::string remap_setting = e->read_setup("Keyboard", "remap", "none");
    int remap = KEY_REMAP_NONE;
    for (int i = 0; i < KEY_REMAP_COUNT; i++)
        if (remap_setting == REMAP_NAMES[i]) remap = i;
    e->set_key_remap(remap);
    if (key_remap_menu == nullptr) {
        key_remap_menu = new QMenu(this);
        const QList<QAction*> acts = ui->menuHelp->actions();
        const int at = acts.indexOf(video_menu->menuAction());
        ui->menuHelp->insertMenu((at >= 0 && at + 1 < acts.size())?acts[at + 1]:nullptr, key_remap_menu);
    }
    key_remap_menu->setTitle(tr("Keyboard"));
    key_remap_menu->clear();
    {
        QActionGroup * remap_group = new QActionGroup(key_remap_menu);
        const QString titles[KEY_REMAP_COUNT] = {
            tr("No remapping"), tr("Numeric keypad on arrows"), tr("Numeric keypad on letters")};
        for (int i = 0; i < KEY_REMAP_COUNT; i++) {
            QAction * ra = key_remap_menu->addAction(titles[i], [this, i]{
                e->set_key_remap(i);
                e->write_setup("Keyboard", "remap", REMAP_NAMES[i]);
            });
            ra->setActionGroup(remap_group);
            ra->setCheckable(true);
            ra->setChecked(remap == i);
        }
    }

    ui->menuScale->clear();
    QActionGroup * scale_group = new QActionGroup(ui->menuScale);

    int i0 = 0;
    QAction * a = ui->menuScale->addAction(
        MainWindow::tr("Auto scale"),
        [this, i0]{e->set_scale(i0);}
        );
    a->setActionGroup(scale_group);
    a->setCheckable(true);
    a->setChecked(e->get_scale() == i0);
    for (unsigned int i=1; i <= 5; i++)
    {
        a = ui->menuScale->addAction(
            QString::number(i) + "x",
            [this, i]{e->set_scale(i);}
            );
        a->setActionGroup(scale_group);
        a->setCheckable(true);
        a->setChecked(e->get_scale() == (int)i);
    };

    ui->menuScreen_ratio->clear();
    QActionGroup * ratio_group = new QActionGroup(ui->menuScreen_ratio);
    QAction * a1 = ui->menuScreen_ratio->addAction(
        tr("Screen 4:3"),
        [this]{e->set_ratio(SCREEN_RATIO_43);}
        );
    a1->setActionGroup(ratio_group);
    a1->setCheckable(true);
    a1->setChecked(e->get_ratio() == SCREEN_RATIO_43);
    QAction * a2 = ui->menuScreen_ratio->addAction(
        tr("Square pixels"),
        [this]{e->set_ratio(SCREEN_RATIO_SQ);}
        );
    a2->setActionGroup(ratio_group);
    a2->setCheckable(true);
    a2->setChecked(e->get_ratio() == SCREEN_RATIO_SQ);
    QAction * a3 = ui->menuScreen_ratio->addAction(
        tr("Square screen"),
        [this]{e->set_ratio(SCREEN_RATIO_11);}
        );
    a3->setActionGroup(ratio_group);
    a3->setCheckable(true);
    a3->setChecked(e->get_ratio() == SCREEN_RATIO_11);

    #ifdef RENDERER_SDL2
        ui->menuFiltering->clear();
        QActionGroup * filtering_group = new QActionGroup(ui->menuFiltering);
        QAction * af1 = ui->menuFiltering->addAction(
            tr("Nearest pixel"),
            [this]{e->set_filtering(SCREEN_FILTERING_NONE);}
            );
        af1->setActionGroup(filtering_group);
        af1->setCheckable(true);
        af1->setChecked(e->get_filtering() == SCREEN_FILTERING_NONE);

        QAction * af2 = ui->menuFiltering->addAction(
            tr("Linear"),
            [this]{e->set_filtering(SCREEN_FILTERING_LINEAR);}
            );
        af2->setActionGroup(filtering_group);
        af2->setCheckable(true);
        af2->setChecked(e->get_filtering() == SCREEN_FILTERING_LINEAR);

        QAction * af3 = ui->menuFiltering->addAction(
            tr("Anisotropic"),
            [this]{e->set_filtering(SCREEN_FILTERING_ANISOTROPIC);}
            );
        af3->setActionGroup(filtering_group);
        af3->setCheckable(true);
        af3->setChecked(e->get_filtering() == SCREEN_FILTERING_ANISOTROPIC);

        QAction * af4 = ui->menuFiltering->addAction(
            tr("Sharp"),
            [this]{e->set_filtering(SCREEN_FILTERING_SHARP);}
            );
        af4->setActionGroup(filtering_group);
        af4->setCheckable(true);
        af4->setChecked(e->get_filtering() == SCREEN_FILTERING_SHARP);
    #elif defined(RENDERER_QT)
        ui->menuFiltering->clear();
        QActionGroup * filtering_group = new QActionGroup(ui->menuFiltering);
        QAction * af1 = ui->menuFiltering->addAction(
            tr("Fast, no smoothing"),
            [this]{e->set_filtering(SCREEN_FILTERING_NONE);}
            );
        af1->setActionGroup(filtering_group);
        af1->setCheckable(true);
        af1->setChecked(e->get_filtering() == SCREEN_FILTERING_NONE);

        QAction * af2 = ui->menuFiltering->addAction(
            tr("Bilinear filtering"),
            [this]{e->set_filtering(SCREEN_FILTERING_SOFT_SMOOTH);}
            );
        af2->setActionGroup(filtering_group);
        af2->setCheckable(true);
        af2->setChecked(e->get_filtering() == SCREEN_FILTERING_LINEAR);
    #elif defined(RENDERER_OPENGL)
        //The texture filter of GLWidget: NONE is nearest, the tube and the
        //sharp mode are shaders of their own, anything else is linear - an
        //anisotropic filter does nothing for a picture that is never minified
        ui->menuFiltering->clear();
        QActionGroup * filtering_group = new QActionGroup(ui->menuFiltering);
        QAction * af1 = ui->menuFiltering->addAction(
            tr("Nearest pixel"),
            [this]{e->set_filtering(SCREEN_FILTERING_NONE);}
            );
        af1->setActionGroup(filtering_group);
        af1->setCheckable(true);
        af1->setChecked(e->get_filtering() == SCREEN_FILTERING_NONE);

        QAction * af2 = ui->menuFiltering->addAction(
            tr("Linear"),
            [this]{e->set_filtering(SCREEN_FILTERING_LINEAR);}
            );
        af2->setActionGroup(filtering_group);
        af2->setCheckable(true);
        af2->setChecked(e->get_filtering() != SCREEN_FILTERING_NONE
                        && e->get_filtering() != SCREEN_FILTERING_SHARP
                        && e->get_filtering() != SCREEN_FILTERING_CRT);

        QAction * af4 = ui->menuFiltering->addAction(
            tr("Sharp"),
            [this]{e->set_filtering(SCREEN_FILTERING_SHARP);}
            );
        af4->setActionGroup(filtering_group);
        af4->setCheckable(true);
        af4->setChecked(e->get_filtering() == SCREEN_FILTERING_SHARP);

        QAction * af3 = ui->menuFiltering->addAction(
            tr("Picture tube"),
            [this]{e->set_filtering(SCREEN_FILTERING_CRT);}
            );
        af3->setActionGroup(filtering_group);
        af3->setCheckable(true);
        af3->setChecked(e->get_filtering() == SCREEN_FILTERING_CRT);
    #endif
    video_update_action();
}

void MainWindow::CreateDevicesMenu()
{
    ui->menuDevices->clear();

    for (unsigned int i=0; i < e->dm->device_count; i++)
    {
        QAction * a = ui->menuDevices->addAction(
                            QString::fromStdString(e->dm->get_device(i)->device_name + " : " + e->dm->get_device(i)->device_type),
                            [this, i]{onDeviceMenuCalled(i);}
                      );
        a->setEnabled( DWM->get_create_func(e->dm->get_device(i)->device_type) != nullptr );
    };
}

void MainWindow::UpdateToolbar()
{
    if (fdds_found > 0) {
        for (unsigned int i=0; i < fdds_found; i++) {
            delete fdd_button[i];
            delete fdd_menu[i];
        }
        fdds.clear();
    }
    if (fdd_timer != nullptr) fdd_timer->stop();

    if (hdds_found > 0) {
        for (unsigned int i = 0; i < hdds_found; i++) {
            delete hdd_button[i];
            delete hdd_menu[i];
        }
        hdds.clear();
        hdds_found = 0;
    }

    if (tape_action != nullptr) {
        ui->toolBar->removeAction(tape_action);
    }

    if (keyboard_action != nullptr) {
        ui->toolBar->removeAction(keyboard_action);
        keyboard_action = nullptr;
    }

    if (buttons_separator != nullptr) {
        ui->toolBar->removeAction(buttons_separator);
    }

    for (int i = 0; i < option_toolbar_actions.size(); i++) {
        ui->toolBar->removeAction(option_toolbar_actions[i]);
        delete option_toolbar_actions[i];
    }
    option_toolbar_actions.clear();
    option_combos.clear();

    int buttons_added=0;

    fdds_found = 0;

    std::vector<ComputerDevice*>fdd_devices = e->dm->find_devices_by_class("fdd");
    //The buttons and menus are fixed arrays: drives beyond them get none,
    //instead of being written past the end
    const unsigned int max_fdds = sizeof(fdd_button)/sizeof(fdd_button[0]);
    fdds_found = std::min((unsigned int)fdd_devices.size(), max_fdds);

    for (unsigned int i=0; i < fdds_found; i++) {
        FDD * fdd = dynamic_cast<FDD*>(fdd_devices[i]);
        fdds.push_back(fdd);
        CreateFDDMenu(i);
        fdd_show(i);
    }
    //Винчестеры. Образ - файл на сотни мегабайт, с машиной он не поставляется,
    //поэтому гнездо обычно пустое, и вставляют образ отсюда
    std::vector<ComputerDevice*> hdd_devices = e->dm->find_devices_by_class("hdd");
    hdds_found = hdd_devices.size();
    if (hdds_found > sizeof(hdd_button)/sizeof(hdd_button[0]))
        hdds_found = sizeof(hdd_button)/sizeof(hdd_button[0]);

    for (unsigned int i = 0; i < hdds_found; i++) {
        hdds.push_back(hdd_devices[i]);
        CreateHDDMenu(i);
        buttons_added++;
        hdd_show(i);
    }

    if (fdds_found > 0 || hdds_found > 0) {
        if (fdd_timer == nullptr)
        {
            fdd_timer = new QTimer(this);
            connect(fdd_timer, &QTimer::timeout, this, &MainWindow::update_fdds);
        }
        fdd_timer->start(100);
    }

    std::vector<ComputerDevice*>tape_devices = e->dm->find_devices_by_class("tape");
    if (tape_devices.size() != 0) {
        buttons_added++;
        QToolButton * tape_button = new QToolButton();
        tape_button->setIcon(QIcon(":/icons/tape"));
        tape_button->setFocusPolicy(Qt::NoFocus);

        connect(tape_button, &QToolButton::clicked, this, &MainWindow::on_actionTape_triggered);
        tape_action = ui->toolBar->insertWidget(ui->actionDebugger, tape_button);
    }

#ifdef HAVE_QT_SVG
    //Only for a machine that carries a drawing of its keyboard
    Keyboard * kbd = dynamic_cast<Keyboard*>(e->dm->get_device_by_name("keyboard", false));
    const bool has_picture = (kbd != nullptr) && !kbd->picture_file().empty();
    ui->actionKeyboard->setVisible(has_picture);
    if (has_picture) {
        buttons_added++;
        QToolButton * keyboard_button = new QToolButton();
        keyboard_button->setIcon(QIcon(":/icons/keyboard"));
        keyboard_button->setToolTip(tr("On-screen keyboard"));
        keyboard_button->setFocusPolicy(Qt::NoFocus);

        connect(keyboard_button, &QToolButton::clicked, this, &MainWindow::on_actionKeyboard_triggered);
        keyboard_action = ui->toolBar->insertWidget(ui->actionDebugger, keyboard_button);
    }
#else
    ui->actionKeyboard->setVisible(false);
#endif

    // Device options
    bool has_hw_buttons = (fdds_found > 0 || hdds_found > 0 || tape_devices.size() != 0);
    bool options_separator_added = false;

    SystemData * sd = e->get_system_data();
    QFileInfo opt_fi(QString::fromStdString(sd->system_file));
    QString config_key = opt_fi.baseName();

    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * dev = e->dm->get_device(i)->device.get();
        DeviceOptions options = dev->get_device_options();
        for (size_t j = 0; j < options.size(); j++) {
            const DeviceOption & opt = options[j];
            if ((opt.type == DEVICE_OPTION_DROPDOWN || opt.type == DEVICE_OPTION_TOGGLE) && !opt.values.empty()) {
                if (has_hw_buttons && !options_separator_added) {
                    QAction * sep = ui->toolBar->insertSeparator(ui->actionDebugger);
                    option_toolbar_actions.append(sep);
                    options_separator_added = true;
                }
                buttons_added++;

                QString option_tooltip = QCoreApplication::translate("DeviceOptions", opt.title.c_str());

                //The picture comes with the machine; an option that names none, or
                //names a file that is not there, gets the generic settings icon
                QString icon_path = opt.icon.empty()?QString():QString::fromStdString(find_file_location(sd, opt.icon));
                if (icon_path.isEmpty()) icon_path = ":/icons/kcmsystem";
                QIcon icon(icon_path);
                icon.addPixmap(QPixmap(icon_path), QIcon::Disabled);
                QAction * icon_action = new QAction(icon, "", this);
                icon_action->setToolTip(option_tooltip);
                ui->toolBar->insertAction(ui->actionDebugger, icon_action);
                option_toolbar_actions.append(icon_action);

                unsigned option_id = opt.id;
                std::string device_name = dev->name;

                //Two positions, off and on: the picture is the switch, pressed
                //while it is on
                if (opt.type == DEVICE_OPTION_TOGGLE) {
                    icon_action->setText(option_tooltip);
                    icon_action->setCheckable(true);
                    icon_action->setChecked(opt.current != 0);
                    connect(icon_action, &QAction::toggled,
                        [this, dev, option_id, config_key, device_name](bool on) {
                            unsigned value_id = on ? 1 : 0;
                            e->invoke([&]() { dev->set_device_option(option_id, value_id); });
                            std::string key = config_key.toStdString() + "_" + device_name + "_" + std::to_string(option_id);
                            e->write_setup("DeviceOptions", key, std::to_string(value_id));
                            e->record_command(device_name, "option", std::to_string(option_id) + "," + std::to_string(value_id));
                        });
                    OptionCombo oc;
                    oc.device = device_name;
                    oc.option = option_id;
                    oc.combo = nullptr;
                    oc.toggle = icon_action;
                    option_combos.append(oc);
                    continue;
                }

                QComboBox * combo = new QComboBox();
                combo->setFocusPolicy(Qt::NoFocus);
                combo->setToolTip(option_tooltip);

                //The device already has the saved choice (apply_saved_device_options),
                //and without one its own start value, which need not be the first
                int selected_index = 0;
                for (size_t v = 0; v < opt.values.size(); v++) {
                    combo->addItem(
                        QCoreApplication::translate("DeviceOptions", opt.values[v].title.c_str()),
                        opt.values[v].id
                    );
                    if (opt.values[v].id == opt.current) {
                        selected_index = static_cast<int>(v);
                    }
                }
                combo->setCurrentIndex(selected_index);

                connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                    [this, combo, dev, option_id, config_key, device_name](int index) {
                        unsigned value_id = combo->itemData(index).toUInt();
                        //A connector plugs and unplugs devices here, lines and all
                        e->invoke([&]() { dev->set_device_option(option_id, value_id); });
                        std::string key = config_key.toStdString() + "_" + device_name + "_" + std::to_string(option_id);
                        e->write_setup("DeviceOptions", key, std::to_string(value_id));
                        e->record_command(device_name, "option", std::to_string(option_id) + "," + std::to_string(value_id));
                    });

                if (icon_action != nullptr) {
                    connect(icon_action, &QAction::triggered, [combo]() {
                        int count = combo->count();
                        if (count > 1) combo->setCurrentIndex((combo->currentIndex() + 1) % count);
                    });
                }

                QAction * action = ui->toolBar->insertWidget(ui->actionDebugger, combo);
                option_toolbar_actions.append(action);

                OptionCombo oc;
                oc.device = device_name;
                oc.option = option_id;
                oc.combo = combo;
                oc.toggle = nullptr;
                option_combos.append(oc);
            }
        }
    }

    if (buttons_added > 0) {
        buttons_separator = ui->toolBar->insertSeparator(ui->actionDebugger);
    }
}

void MainWindow::onDeviceMenuCalled(unsigned int i)
{
    DebugWndCreateFunc * f = DWM->get_create_func(e->dm->get_device(i)->device_type);
    if (f != nullptr)
    {
        GenericDbgWnd * w = f(this, e, e->dm->get_device(i)->device.get());
        w->setAttribute(Qt::WA_DeleteOnClose);
        remember_geometry(w, QString::fromStdString(e->dm->get_device(i)->device_name));
        DWM->add_window(w);
        connect(w, &GenericDbgWnd::data_changed, [this](GenericDbgWnd * src) {
            DWM->data_changed(src);
        });
        connect(w, &QObject::destroyed, [this, w]() {
            DWM->remove_window(w);
        });
        w->show();
    }
}

void MainWindow::keyPressEvent( QKeyEvent *event )
{
    if (e == nullptr) return;       // the window is closing
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //Ctrl-Alt gives the captured mouse back. The keys still reach the
        //machine: swallowing one would leave the other held there
        if (mouse_captured
            && ((event->key() == Qt::Key_Control && (event->modifiers() & Qt::AltModifier))
                || (event->key() == Qt::Key_Alt && (event->modifiers() & Qt::ControlModifier))))
            mouse_capture(false);

        // qDebug() << "Key pressed: scan " << event->nativeScanCode() << "virtual" << event->nativeVirtualKey() << "key" << Qt::hex << event->key();
        const int key = host_layout_key(event);
        e->host_key(key, event->nativeScanCode(), event->modifiers(), true);

        if (key == EmuKey::Cancel) {
            //Pause/Break resets the machine, see Emulator::key_event(); the
            //key has no script name, so the reset itself is recorded
            std::vector<std::string> args;
            args.push_back((event->modifiers() & Qt::AltModifier)?"cold":"soft");
            e->record_verb(SCRIPT_CMD_RESET, args);
        } else {
            e->record_key(static_cast<unsigned int>(key), event->nativeScanCode(), true);
        }
    }
}

void MainWindow::keyReleaseEvent( QKeyEvent *event )
{
    if (e == nullptr) return;       // the key that closed the window
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key released:" << event->nativeScanCode() << event->nativeVirtualKey() << event->key();
        //The physical key comes up as what it went down as, whatever Qt calls it now
        const int key = e->host_key(host_layout_key(event), event->nativeScanCode(), event->modifiers(), false);
        e->record_key(static_cast<unsigned int>(key), event->nativeScanCode(), false);
    }
}

//----------------------------- Machine mouse --------------------------------//

//Portions of movement are sent this often: the machine takes a step no faster
//than its polling anyway, and a recording gets a MOUSE line per portion rather
//than one per host event
#define MOUSE_FLUSH_MS 40

//A step count in script syntax. The machine may count in octal, a movement is
//written in decimal, so it carries the "_" mark; a minus goes in front of it
static std::string mouse_steps_arg(int n)
{
    if (n == 0) return "0";
    return std::string((n < 0)?"-_":"_") + std::to_string((n < 0)?-n:n);
}

//Positions are taken on the screen the widget is on: without one Qt converts
//with the scale of the primary monitor, and a second monitor with another
//scale puts the pointer somewhere else entirely
static void mouse_set_pos(QWidget * w, const QPoint &p)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    if (w->screen() != nullptr) { QCursor::setPos(w->screen(), p); return; }
#else
    Q_UNUSED(w);
#endif
    QCursor::setPos(p);
}

static QPoint mouse_event_pos(const QMouseEvent * me)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return me->globalPosition().toPoint();
#else
    return me->globalPos();
#endif
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != screen || e == nullptr) return QMainWindow::eventFilter(watched, event);

    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        {
            const QMouseEvent * me = static_cast<QMouseEvent*>(event);
            if (!mouse_captured) {
                //Only a click that captures is taken, the machine does not see it
                if (me->button() == Qt::LeftButton && e->loaded && e->has_mouse()) {
                    mouse_capture(true);
                    return true;
                }
                break;
            }
            if (me->button() == Qt::MiddleButton) {
                mouse_capture(false);
                return true;
            }
            const int bit = (me->button() == Qt::LeftButton)?1:((me->button() == Qt::RightButton)?2:0);
            if (bit != 0 && (mouse_buttons & bit) == 0) {
                mouse_buttons |= bit;
                mouse_flush(true);
            }
            return true;
        }

        case QEvent::MouseButtonRelease:
        {
            if (!mouse_captured) break;
            const QMouseEvent * me = static_cast<QMouseEvent*>(event);
            const int bit = (me->button() == Qt::LeftButton)?1:((me->button() == Qt::RightButton)?2:0);
            if (bit != 0 && (mouse_buttons & bit) != 0) {
                mouse_buttons &= ~bit;
                mouse_flush(true);
            }
            return true;
        }

        case QEvent::MouseMove:
        {
            if (!mouse_captured) break;
            //The movement is the distance from the previous event. The pointer
            //is put back in the middle only when it strays far: a fractional
            //scale of the desktop rounds the position it is put to, and doing
            //it on every event made each correction an event of its own
            const QPoint p = mouse_event_pos(static_cast<QMouseEvent*>(event));
            const QPoint d = p - mouse_last;
            mouse_last = p;
            if (std::abs(p.x() - mouse_center.x()) > screen->width() / 4
                || std::abs(p.y() - mouse_center.y()) > screen->height() / 4) {
                mouse_set_pos(screen, mouse_center);
                mouse_last = mouse_center;
            }
            if (d.isNull()) return true;
            //At 100% a line of the machine's screen crossed by the host pointer
            //is a step. The size of a line is measured on the widget, not
            //taken from the scale setting: automatic scaling reports 0 there,
            //and a maximized window draws the screen larger than any setting.
            //Programs move their cursor by several pixels a step, some twice
            //that when the movement goes on, hence the lower default
            unsigned int sx, sy;
            e->get_screen_size(&sx, &sy);
            double line = (sy > 0)?static_cast<double>(screen->height()) / sy:1.0;
            if (line < 1.0) line = 1.0;
            const double k = mouse_speed / 100.0 / line;
            mouse_acc_x += d.x() * k;
            mouse_acc_y += d.y() * k;
            return true;
        }

        case QEvent::ContextMenu:
            if (mouse_captured) return true;
            break;

        default:
            break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (e != nullptr && event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        //A pointer kept hidden in the middle of an inactive window is lost to the user
        mouse_capture(false);
        //A key held while the focus goes away is released in another window
        e->release_host_keys();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::mouse_capture(bool on)
{
    if (on == mouse_captured) return;

    if (on) {
        mouse_captured = true;
        mouse_acc_x = 0;
        mouse_acc_y = 0;
        mouse_buttons = 0;
        screen->setMouseTracking(true);
        screen->grabMouse(QCursor(Qt::BlankCursor));
        mouse_center = screen->mapToGlobal(screen->rect().center());
        mouse_set_pos(screen, mouse_center);
        mouse_last = mouse_center;
        mouse_timer->start(MOUSE_FLUSH_MS);
        statusBar()->showMessage(tr("The mouse is captured by the machine. Press Ctrl-Alt or the middle button to release it"));
        return;
    }

    mouse_timer->stop();
    //What is still on its way, and the buttons let go: the machine must not be
    //left with a button held by a pointer that has gone
    if (e->loaded) {
        mouse_acc_x = 0;
        mouse_acc_y = 0;
        if (mouse_buttons != 0) {
            mouse_buttons = 0;
            mouse_flush(true);
        }
    }
    mouse_buttons = 0;
    mouse_captured = false;
    screen->releaseMouse();
    screen->setMouseTracking(false);
    statusBar()->clearMessage();
}

void MainWindow::mouse_flush(bool buttons_changed)
{
    if (e == nullptr) return;
    //The socket may have been switched to something else meanwhile
    if (mouse_captured && !buttons_changed && !(e->loaded && e->has_mouse())) {
        mouse_capture(false);
        return;
    }

    const int dx = static_cast<int>(mouse_acc_x);
    const int dy = static_cast<int>(mouse_acc_y);
    mouse_acc_x -= dx;
    mouse_acc_y -= dy;
    if (dx == 0 && dy == 0 && !buttons_changed) return;

    e->mouse_event(dx, dy, buttons_changed?mouse_buttons:-1);

    std::vector<std::string> args;
    args.push_back(mouse_steps_arg(dx));
    args.push_back(mouse_steps_arg(dy));
    if (buttons_changed) args.push_back(std::to_string(mouse_buttons));
    e->record_verb(SCRIPT_CMD_MOUSE, args);
}

void MainWindow::resizeEvent(QResizeEvent * event)
{
    //e->resize_screen();
}

void MainWindow::paintEvent(QPaintEvent * event)
{
    //QMainWindow::paintEvent(event);
    if (e != nullptr) e->resize_screen();
}

void MainWindow::on_action_Cold_restart_triggered()
{
    if (!e->loaded) return;
    e->reset(true);
    e->record_verb(SCRIPT_CMD_RESET, std::vector<std::string>(1, "cold"));
}


void MainWindow::on_action_Soft_restart_triggered()
{
    if (!e->loaded) return;
    e->reset(false);
    e->record_verb(SCRIPT_CMD_RESET, std::vector<std::string>(1, "soft"));
}

//The same pair of actions as the "Stop" and "Run without debugging" buttons
//of the CPU window, on a single tool bar button whose icon shows which one is
//available now
void MainWindow::on_actionCPUState_triggered()
{
    if (!e->loaded) return;
    const std::vector<CPU*> &cpus = e->dm->get_cpus();
    if (cpus.empty()) return;

    //The button stops and starts the machine, not one of its processors: on a
    //two-processor machine leaving one of them running would freeze the other
    //at the first channel handshake anyway. The master decides which way the
    //button goes, and all of them follow it
    const bool run = (cpus[0]->m_debug == DEBUG_STOPPED);
    e->invoke([&]() {
        for (size_t i = 0; i < cpus.size(); i++)
            cpus[i]->m_debug = run? DEBUG_OFF : DEBUG_STOPPED;
    });
    for (size_t i = 0; i < cpus.size(); i++)
        e->record_command(cpus[i]->name, run? "run" : "stop", "");
    update_cpu_state_action();
}

void MainWindow::update_cpu_state_action()
{
    //The device manager only exists once a machine has been loaded. The state
    //shown is the master's, which is the one the button acts on first
    CPU * cpu = (e->loaded && !e->dm->get_cpus().empty())? e->dm->get_cpus()[0] : nullptr;

    ui->actionCPUState->setEnabled(cpu != nullptr);

    //Anything but DEBUG_STOPPED is a running processor, DEBUG_BRAKES and
    //DEBUG_STEP included, so the button offers to stop it
    const int state = ((cpu != nullptr) && (cpu->m_debug == DEBUG_STOPPED))? DEBUG_STOPPED : DEBUG_OFF;
    if (state == cpu_state_shown) return;
    cpu_state_shown = state;

    const bool stopped = (state == DEBUG_STOPPED);
    ui->actionCPUState->setIcon(QIcon(stopped? ":/icons/play_fwd" : ":/icons/pause"));
    ui->actionCPUState->setText(stopped? tr("Run without debugging") : tr("Stop"));
    ui->actionCPUState->setToolTip(stopped? tr("Run without debugging") : tr("Stop"));
}

void MainWindow::set_volume(int value)
{
    e->write_setup("Sound", "volume", std::to_string(value));
    e->set_volume(value);
}

void MainWindow::set_mute(bool muted)
{
    e->write_setup("Sound", "muted", std::to_string(muted?1:0));
    e->set_muted(muted);
    volume->setEnabled(!muted);
}

void MainWindow::on_action_Select_a_machine_triggered()
{
    QDialog * w = new OpenConfigWindow(this, e);
    w->setAttribute(Qt::WA_DeleteOnClose);
    connect(w, SIGNAL(load_config(QString,bool)), this, SLOT(load_config(QString,bool)));
    w->show();
}


void MainWindow::set_title()
{
    SystemData * sd = e->get_system_data();
    setWindowTitle("[eCat " + QString(PROJECT_VERSION) + "] " + QString::fromStdString(sd->system_name) + " : " + QString::fromStdString(sd->system_version));
}

void MainWindow::load_config(QString file_name, bool set_default, bool run_embedded)
{
    if (e->loaded)
    {
        if (fdd_timer != nullptr) fdd_timer->stop();

        //The mouse the pointer was captured for is about to be deleted
        mouse_capture(false);

        //Switching the machine invalidates every device the script addresses.
        //The buffer is kept: a replay may be the reason for the switch
        rec_stop_all();
        e->script_recorder()->invalidate();
        e->stop_script();

        e->stop_emulation();

        //Отладочные окна держат указатели на устройства, а те сейчас исчезнут
        //вместе со старой машиной. Окно магнитофона, например, раз в секунду
        //опрашивает лентопротяжку и останавливает ее на закрытии - и то и
        //другое уходило на освобожденную память. Закрываем их здесь, как и при
        //выходе; окно клавиатуры закрытия не требует, оно умеет перечитать
        //рисунок новой машины (см. ниже)
        const QList<GenericDbgWnd*> dbg_windows = findChildren<GenericDbgWnd*>();
        for (auto * w : dbg_windows)
        {
#ifdef HAVE_QT_SVG
            if (qobject_cast<KeyboardWindow*>(w) != nullptr) continue;
#endif
            w->close();
            //At once, as in closeEvent(): a deferred delete waits for the event
            //loop, and the box that reports a machine which failed to load is
            //one - under it the CPU window kept polling a deleted processor
            delete w;
        }
    }

    //The remembered disks are the user's, for a machine the user opens: a
    //script, a replay or an MCP client expects the configuration's own
    bool restore_disks = ui->actionRememberDisks->isChecked() && !no_disk_restore && !cmdline_starting;
#ifdef ENABLE_MCP
    if (mcp_mode) restore_disks = false;
#endif
    no_disk_restore = false;
    e->set_restore_disks(restore_disks);

    emulator::Result res = e->load_config(file_name.toStdString());
    if (!res) {
        //The machine that was running is gone and nothing took its place: the
        //drive buttons, the option boxes and the device menu still held its
        //devices. Rebuilt for a machine without devices, they are simply empty
        CreateDevicesMenu();
        UpdateToolbar();
#ifdef ENABLE_MCP
        //A modal box here would block the GUI thread that the MCP client is
        //waiting on, so the error is reported through the protocol instead
        if (mcp_mode) { mcp_load_error = translateResultMessage(res.message); return; }
#endif
        //Nobody is there to dismiss a box under a command line script: the run
        //waited for it until the runner killed it. It says why and ends
        if (cmdline_starting || rec_cmdline) {
            std::cerr << "Unable to load the machine: " << strip_message_context(res.message) << std::endl;
            rec_timer->stop();
            rec_exit_code = 2;
            QTimer::singleShot(0, this, &QWidget::close);
            return;
        }
        QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
        return;
    }

    cur_config = file_name;

    set_title();

    CreateDevicesMenu();
    UpdateToolbar();

#ifdef HAVE_QT_SVG
    //An open keyboard window belongs to the machine that is gone by now, so it
    //either picks up the new drawing or closes
    KeyboardWindow * kw = findChild<KeyboardWindow*>();
    if (kw != nullptr) {
        kw->reload();
        if (!kw->valid()) kw->close();
    }
#endif

    e->set_volume(volume->value());
    e->set_muted(mute->isChecked());

    #ifdef RENDERER_SDL2
        void* nativeView = reinterpret_cast<void*>(screen->winId());
    #elif defined(RENDERER_QT) or defined(RENDERER_OPENGL)
        void* nativeView = reinterpret_cast<void*>(screen);
    #endif

    if (nativeView) {
        e->init_video(nativeView);
        e->run();

        if (set_default)
        {
            //Relative to computers/ when it is there; a configuration of the
            //user lives elsewhere and is stored with its full path
            QString work = QString::fromStdString(e->work_path);
            QString new_file = file_name.startsWith(work)
                ? file_name.mid(work.length())
                : file_name;
            e->write_setup("Startup", "default", new_file.toStdString());
        }

        //A configuration extension may bring a script to run on the machine.
        //Never under an MCP client, even for a machine picked from the menu:
        //the script would take over the client's command buffer
#ifdef ENABLE_MCP
        if (mcp_mode) run_embedded = false;
#endif
        if (run_embedded && e->has_embedded_script())
        {
            emulator::Result sres = e->load_embedded_script();
            if (!sres)
                QMessageBox::warning(this, tr("Error"), translateResultMessage(sres.message));
            else
                start_script(startup_load);
        }
    } else {
        qWarning() << "ui->screen->winId() is null!";
    }
}


//Qt::SkipEmptyParts is Qt 5.14, and the same enum of QString before it, so
//neither name fits every kit this builds on
static QStringList split_nonempty(const QString &s, const QString &sep)
{
    QStringList r;
    const QStringList parts = s.split(sep);
    for (int i = 0; i < parts.size(); i++)
        if (!parts.at(i).isEmpty()) r << parts.at(i);
    return r;
}

//The masks of one entry of a Qt filter: whatever stands in the last brackets
static QStringList filter_masks(const QString &entry)
{
    const int open = entry.lastIndexOf('(');
    const int close = entry.lastIndexOf(')');
    if (open < 0 || close < open) return QStringList();
    return split_nonempty(entry.mid(open + 1, close - open - 1), " ");
}

void MainWindow::on_actionOpen_triggered()
{
    SystemData * sd = e->get_system_data();

    //A file for the machine, another machine and a saved state all open from
    //here. Kept as separate entries, the selected one was the first - the
    //filter the machine declares in its system section - so a state took a
    //trip through the drop-down every single time. The first entry is now one
    //common filter holding every mask at once, and the particular ones stay
    //behind it for narrowing the list down
    QStringList entries;
    const QString allowed = QString::fromStdString(sd->allowed_files);
    entries += split_nonempty(allowed, ";;");
    entries << tr("Configurations") + " (*.cfg *.ext *.ext.zip)";
    entries << tr("Saved states") + " (*.ecats *.ecats.zip)";

    //"All files" is an entry a machine declares itself, and its mask does not
    //go into the common filter: with it the filter would stop differing from
    //that entry alone. The entry itself stays in the list as it was
    QStringList masks;
    for (int i = 0; i < entries.size(); i++)
    {
        const QStringList entry_masks = filter_masks(entries.at(i));
        for (int j = 0; j < entry_masks.size(); j++)
        {
            const QString mask = entry_masks.at(j);
            if (mask != "*" && mask != "*.*" && !masks.contains(mask)) masks << mask;
        }
    }

    QString filters;
    if (!masks.isEmpty()) filters = tr("All supported files") + " (" + masks.join(' ') + ");;";
    filters += entries.join(";;");

    QString file_name = QFileDialog::getOpenFileName(this, tr("Load a file"), last_path, filters);


    if (!file_name.isEmpty()) {
        QFileInfo fi(file_name);
        last_path = fi.absolutePath();
        e->set_last_path(last_path.toStdString());

        if (is_machine_file(file_name.toStdString())) {
            load_config(fi.absoluteFilePath(), false);
            return;
        }

        emulator::Result res = HandleExternalFile(e, file_name.toStdString());
        if (!res) {
            QMessageBox::warning(this, tr("Error"), translateResultMessage(res.message));
        }
    }
}

void MainWindow::open_debugger_for(CPU * cpu)
{
    if (cpu == nullptr) return;
    DebugWndCreateFunc * f = DWM->get_create_func(cpu->type);
    if (f != nullptr)
    {
            GenericDbgWnd * w = f(this, e, cpu);
            w->setAttribute(Qt::WA_DeleteOnClose);
            remember_geometry(w, QString::fromStdString(cpu->name));
            DWM->add_window(w);
            connect(w, &GenericDbgWnd::data_changed, [this](GenericDbgWnd * src) {
                DWM->data_changed(src);
            });
            connect(w, &QObject::destroyed, [this, w]() {
                DWM->remove_window(w);
            });
            w->show();
    }
}

void MainWindow::remember_geometry(QDialog * w, const QString & key)
{
    //By the kind of window and the device it shows: the debugger of "cpu"
    //opens in the same place on every machine, a second processor's apart
    QString ident = QString::fromLatin1(w->metaObject()->className());
    if (!key.isEmpty()) ident += "." + key;
    const std::string name = "geometry." + ident.toStdString();

    std::string geometry = e->read_setup("Window", name, "");
    if (!geometry.empty())
        w->restoreGeometry(QByteArray::fromHex(QByteArray::fromStdString(geometry)));

    //finished() comes both from a close (QDialog::closeEvent rejects) and from
    //Esc, while the window still exists; the windows closed by the main
    //window on exit and on a machine change go through it before the emulator
    //is deleted
    connect(w, &QDialog::finished, this, [this, w, name]() {
        if (e != nullptr) e->write_setup("Window", name, w->saveGeometry().toHex().toStdString());
    });
}

void MainWindow::on_actionSaveState_triggered()
{
    if (!e->loaded) return;

    //Named after the machine and the moment, in the directory the other file
    //dialogs use. Not work_path: that is computers/, which the machine chooser
    //lists - snapshots do not belong among the machines
    const QString name = QString::fromStdString(
            machine_file_stem(dsk_tools::get_filename(cur_config.toStdString())))
        + "-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss") + ".ecats.zip";
    const QString suggested = QDir(last_path).filePath(name);

    QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save state"), suggested,
        MainWindow::tr("Saved states") + " (*.ecats.zip);;" + MainWindow::tr("Saved states, unpacked") + " (*.ecats)");
    if (file_name.isEmpty()) return;
    //A name typed without one: the extension picks the form, so it cannot be
    //left to chance
    if (!is_state_file(file_name.toStdString())) file_name += ".ecats.zip";
    last_path = QFileInfo(file_name).absolutePath();

    //Taken on the emulation thread, between two slices: a snapshot pulled out
    //from here would catch the machine halfway through one
    e->request_state(file_name.toStdString());

    //A whole machine takes a moment to write. The request is picked up at a
    //slice boundary, so a machine that is not running never takes it - and
    //then the wait has to end with a message rather than with nothing
    for (int i = 0; i < 500 && e->state_pending(); i++) QThread::msleep(10);

    const std::string error = e->take_state_error();
    if (!error.empty())
        QMessageBox::critical(this, MainWindow::tr("Save state"), translateResultMessage(error));
    else if (e->state_pending())
        QMessageBox::critical(this, MainWindow::tr("Save state"),
            MainWindow::tr("The machine is not running, so its state cannot be taken."));
}

void MainWindow::on_actionDebugger_triggered()
{
    if (!e->loaded) return;
    const std::vector<CPU*> &cpus = e->dm->get_cpus();
    if (cpus.empty()) return;

    //One processor: straight to its window, as it always was. Several: a menu
    //to pick one, since each has its own address space and its own breakpoints.
    //The window titles itself after the device, so the two are told apart
    if (cpus.size() == 1) {
        open_debugger_for(cpus[0]);
        return;
    }

    QMenu menu(this);
    for (size_t i = 0; i < cpus.size(); i++) {
        QAction * a = menu.addAction(QString::fromStdString(cpus[i]->name + " : " + cpus[i]->type));
        CPU * c = cpus[i];
        connect(a, &QAction::triggered, [this, c]() { open_debugger_for(c); });
    }
    menu.exec(QCursor::pos());
}

void MainWindow::closeEvent (QCloseEvent *event)
{
    fdds_found = 0; // to prevent crashing on buttons update
    hdds_found = 0;

    // The timers must not fire once the emulator is gone
    if (fdd_timer != nullptr) fdd_timer->stop();
    if (rec_timer != nullptr) rec_timer->stop();
    mouse_capture(false);
    if (mouse_timer != nullptr) mouse_timer->stop();

    // A window never shown (an MCP session no client asked a machine of)
    // has no geometry worth keeping
    if (e != nullptr && isVisible())
        e->write_setup("Window", "geometry", saveGeometry().toHex().toStdString());

    // Close all debug windows before destroying the emulator,
    // so their closeEvent handlers can safely access devices
    QList<GenericDbgWnd*> dbgWindows = findChildren<GenericDbgWnd*>();
    for (auto *w : dbgWindows) {
        w->close();
        //Deleted here, not left to WA_DeleteOnClose: that is a deferred delete,
        //and until the event loop gets to it the window is only hidden - its
        //timers still fire (the CPU window polls every 200 ms, the keyboard
        //every 40) and it still hears the loss of activation, all of it
        //through pointers to the emulator and its devices deleted just below
        delete w;
    }

    e->stop_emulation();

    // The video stopped with the machine and is being finished in the
    // background; deleting the emulator would wait for it with the window
    // frozen, and a window that does not answer gets killed
    if (video_timer != nullptr) video_timer->stop();
    if (video_frame_timer != nullptr) video_frame_timer->stop();
#if defined(RENDERER_OPENGL)
    static_cast<GLWidget*>(screen)->setFrameHook(nullptr);
#endif
    if (e->video_state() == VideoRecorder::FINISHING) {
        QProgressDialog wait(tr("Finishing the video..."), QString(), 0, 0, this);
        wait.setWindowTitle(tr("Video recording"));
        wait.setCancelButton(nullptr);
        wait.setMinimumDuration(0);
        wait.setWindowModality(Qt::WindowModal);
        wait.show();
        while (e->video_state() == VideoRecorder::FINISHING) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    }

    // Under MCP the emulator is left alive, stopped, until the process ends
    // (closing the window quits it). The MCP session was given the same
    // pointer when it started: its destructor, run after this window's, clears
    // the script sink through it, and the stdin reader thread may be in the
    // middle of a request. Deleting here left both on freed memory, and a
    // close after a long session - once that memory had been reused - crashed
    // with 0xC0000005. Taking the session's lock instead could deadlock: it is
    // held across blocking calls into this thread (load_machine)
#ifdef ENABLE_MCP
    if (mcp_bridge == nullptr) delete e;
#else
    delete e;
#endif
    // The window keeps receiving events after this: it loses the activation
    // (changeEvent releases the host keys), the key that closed it comes up,
    // a paint arrives. Each of those used to reach the freed emulator, and a
    // click on the close button of the active window crashed the process with
    // 0xC0000005 once the memory had been reused - rarely, hence unnoticed
    e = nullptr;

    event->accept();
}

void MainWindow::on_action_Exit_triggered()
{
    close();
}

void MainWindow::fdd_open(unsigned int n)
{
    if (fdds[n] != nullptr)
    {
        QString file_name = QFileDialog::getOpenFileName(this, MainWindow::tr("Open disk image"), last_path, QString::fromStdString(fdds[n]->files));
        if (!file_name.isEmpty())
        {
            QFileInfo fi(file_name);
            fdd_menu[n]->actions().at(0)->setText(fi.fileName());
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_mount"));
            //The controller may be in the middle of a sector of the old disk
            emulator::Result res = emulator::Result::ok();
            e->invoke([&]() { res = fdds[n]->load_image(file_name.toStdString()); });
            if (!res) {
                QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
            } else {
                e->record_command(fdds[n]->name, "load", format_script_arg(fi.absoluteFilePath().toStdString()));
                disk_changed(fdds[n]);
            }
            last_path = fi.absolutePath();
            e->set_last_path(last_path.toStdString());
        }
    }
}

void MainWindow::fdd_eject(unsigned int n)
{
    fdd_menu[n]->actions().at(0)->setText(MainWindow::tr("<Not loaded>"));
    fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    e->invoke([&]() { fdds[n]->unload(); });
    e->record_command(fdds[n]->name, "eject", "");
    disk_changed(fdds[n]);
}

void MainWindow::fdd_wp(unsigned int n)
{
    e->invoke([&]() { fdds[n]->change_protection(); });
    e->record_command(fdds[n]->name, "protect", fdds[n]->is_protected()?"1":"0");
    disk_changed(fdds[n]);
    if (fdds[n]->get_loaded()) {
        if (fdds[n]->is_protected()) {
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_locked"));
        } else {
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_mount"));
        }
    }
}

//The menu and the button of a floppy drive from the state of its device
void MainWindow::fdd_show(unsigned int n)
{
    FDD * fdd = fdds[n];
    if (fdd->get_loaded()) {
        fdd_menu[n]->actions().at(0)->setText(QString::fromStdString(fdd->file_name));
        fdd_button[n]->setIcon(QIcon(fdd->is_protected() ? ":/icons/floppy_locked" : ":/icons/floppy_mount"));
    } else {
        fdd_menu[n]->actions().at(0)->setText(MainWindow::tr("<Not loaded>"));
        fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    }
}

void MainWindow::fdd_default(unsigned int n)
{
    e->restore_default_disk(fdds[n]);
    fdd_show(n);
}

void MainWindow::hdd_default(unsigned int n)
{
    e->restore_default_disk(hdds[n]);
    hdd_show(n);
}

void MainWindow::disk_changed(ComputerDevice * dev)
{
    if (ui->actionRememberDisks->isChecked()) e->remember_disk(dev);
}

static bool device_flag(ComputerDevice * d, const std::string &field)
{
    DeviceFieldValue v;
    return d->get_field(field, 0, 0, v) && !v.values.empty() && v.values[0] != 0;
}

static std::string device_text(ComputerDevice * d, const std::string &field)
{
    DeviceFieldValue v;
    return d->get_field(field, 0, 0, v) ? v.text : std::string();
}

//The menu and the button of a hard disk socket from the state of its device
void MainWindow::hdd_show(unsigned int n)
{
    ComputerDevice * d = hdds[n];
    const bool attached = device_flag(d, "attached");
    const std::string file = device_text(d, "file");
    hdd_menu[n]->actions().at(0)->setText(attached
        ? QFileInfo(QString::fromStdString(file)).fileName()
        : MainWindow::tr("<Not loaded>"));
    const char * icon = attached ? ":/icons/hdd_mount" : ":/icons/hdd_unmount";
    hdd_button[n]->setIcon(QIcon(icon));
    hdd_button[n]->setProperty("led_icon", QString(icon));
    //Образ, который не открылся на запись, защищён и без спроса
    hdd_menu[n]->actions().at(3)->setChecked(device_flag(d, "protected"));
}

void MainWindow::hdd_open(unsigned int n)
{
    //The file dialog filter is the one the editor of extensions uses too
    std::string files;
    const ConfigFields fields = hdds[n]->get_config_fields();
    for (size_t i = 0; i < fields.size(); i++)
        if (fields[i].name == "image") files = fields[i].files;

    QString file_name = QFileDialog::getOpenFileName(this, MainWindow::tr("Open a hard disk image"), last_path, QString::fromStdString(files));
    if (file_name.isEmpty()) return;

    QFileInfo fi(file_name);
    const std::string arg = format_script_arg(fi.absoluteFilePath().toStdString());
    emulator::Result res = hdds[n]->send_command("load", arg);
    if (!res)
        QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
    else {
        e->record_command(hdds[n]->name, "load", arg);
        disk_changed(hdds[n]);
    }
    hdd_show(n);
    last_path = fi.absolutePath();
    e->set_last_path(last_path.toStdString());
}

void MainWindow::hdd_eject(unsigned int n)
{
    hdds[n]->send_command("eject", "");
    e->record_command(hdds[n]->name, "eject", "");
    disk_changed(hdds[n]);
    hdd_show(n);
}

void MainWindow::hdd_wp(unsigned int n)
{
    const bool on = hdd_menu[n]->actions().at(3)->isChecked();
    hdds[n]->send_command("protect", on?"1":"0");
    e->record_command(hdds[n]->name, "protect", on?"1":"0");
    disk_changed(hdds[n]);
}

void MainWindow::fdd_write(unsigned int n)
{
    if (fdds[n] != nullptr)
    {
        QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save disk image to a file"), last_path, QString::fromStdString(fdds[n]->files_save), 0, QFileDialog::DontConfirmOverwrite);
        if (!file_name.isEmpty())
        {
            std::string std_file_name = file_name.toStdString();
            QMessageBox::StandardButton reply;

            if (dsk_tools::file_exists(std_file_name))
            {
                reply = QMessageBox::question(this,
                                              MainWindow::tr("File already exists"),
                                              MainWindow::tr("File already exists. Overwrite? (Choose \"No\" to make a backup)"),
                                              QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
            } else {
                reply = QMessageBox::Yes;
            }

            emulator::Result save_res;
            //Written between two instructions, not while a sector is going in
            if (reply == QMessageBox::Yes)
            {
                e->invoke([&]() { save_res = fdds[n]->save_image(std_file_name); });
            } else
            if (reply == QMessageBox::No)
            {
                QString backup_name = file_name + ".bak";
                bool result = QFile::rename(file_name, backup_name);
                if (result)
                    e->invoke([&]() { save_res = fdds[n]->save_image(std_file_name); });
                else
                    QMessageBox::critical(this, MainWindow::tr("Backup error"), MainWindow::tr("Error creating a backup. Probably *.bak already exists."));
            }
            if (!save_res) {
                QMessageBox::critical(this, tr("Error"), translateResultMessage(save_res.message));
            } else if (reply != QMessageBox::Cancel) {
                e->record_command(fdds[n]->name, "save", format_script_arg(QFileInfo(file_name).absoluteFilePath().toStdString()));
            }
            QFileInfo fi(file_name);
            last_path = fi.absolutePath();
            e->set_last_path(last_path.toStdString());
        }
    }

}

void MainWindow::update_fdds()
{
    // TODO: this event may happen after exiting or stopping emulator
    //One phase for every lamp of the tick: flipped per lit drive, two lit
    //drives flipped it back and both pictures froze - the first one lit, the
    //other one dark
    fdd_blinker = !fdd_blinker;
    for (unsigned int i=0; i<fdds_found; i++)
    {
        if (fdds[i]->is_led_on()) {
            if (fdd_blinker) {
                fdd_button[i]->setIcon(QIcon(":/icons/floppy_access"));
            } else {
                fdd_button[i]->setIcon(QIcon(":/icons/floppy_mount"));
            }
        } else {
            if (fdds[i]->get_loaded()) {
                if (fdds[i]->is_protected()) {
                    fdd_button[i]->setIcon(QIcon(":/icons/floppy_locked"));
                } else {
                    fdd_button[i]->setIcon(QIcon(":/icons/floppy_mount"));
                }
            } else {
                fdd_button[i]->setIcon(QIcon(":/icons/floppy_unmount"));
            }
        }
    }
    for (unsigned int i = 0; i < hdds_found; i++)
    {
        HddImageOwner * owner = dynamic_cast<HddImageOwner*>(hdds[i]);
        if (owner == nullptr) continue;
        HddImage & image = owner->hdd_image();
        const char * icon = !image.attached() ? ":/icons/hdd_unmount"
                          : (image.is_led_on() && fdd_blinker) ? ":/icons/hdd_access"
                          : ":/icons/hdd_mount";
        //Set only on a change: the button would otherwise repaint ten times a second
        if (hdd_button[i]->property("led_icon").toString() != icon) {
            hdd_button[i]->setIcon(QIcon(icon));
            hdd_button[i]->setProperty("led_icon", QString(icon));
        }
    }
}

//The picture the way the window shows it: scaled, with the pixel aspect and the
//filter, without the border. A null image means "save the original instead"
QImage MainWindow::picture_as_shown(const std::vector<uint8_t> &raw, unsigned int sx, unsigned int sy)
{
#if defined(RENDERER_OPENGL)
    Q_UNUSED(raw); Q_UNUSED(sx); Q_UNUSED(sy);
    return static_cast<GLWidget*>(screen)->grabPicture();
#elif defined(RENDERER_QT)
    Q_UNUSED(raw); Q_UNUSED(sx); Q_UNUSED(sy);
    return static_cast<QtRenderWidget*>(screen)->displayed_image();
#elif defined(RENDERER_SDL2)
    //The window belongs to SDL and cannot be read back from here; the picture
    //is scaled again to the size it is drawn at, with the filter chosen
    if (raw.size() != (size_t)sx * sy * 4) return QImage();
    int w, h;
    static_cast<SDL2Renderer*>(renderer)->get_render_size(&w, &h);
    if (w <= 0 || h <= 0) return QImage();
    const QImage original(raw.data(), sx, sy, sx * 4, QImage::Format_RGBA8888);
    return original.scaled(w, h, Qt::IgnoreAspectRatio,
        (e->get_filtering() == SCREEN_FILTERING_NONE)?Qt::FastTransformation:Qt::SmoothTransformation);
#else
    Q_UNUSED(raw); Q_UNUSED(sx); Q_UNUSED(sy);
    return QImage();
#endif
}

void MainWindow::on_actionScreenshot_triggered()
{
    unsigned int sx, sy;
    e->get_screen_constraints(&sx, &sy);
    std::vector<uint8_t> image = renderer->get_screenshot();
    QImage shown;
    if (screenshot_as_shown) shown = picture_as_shown(image, sx, sy);

    //The image is taken before the dialog, so the recording refers to that moment
    uint64_t taken_at = e->clock_now();

    //The folder of the previous screenshot, and a name like a video's:
    //the machine and the moment
    QString folder = QString::fromStdString(e->read_setup("Video", "screenshot_folder", ""));
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QString::fromStdString(e->work_path);
    QString machine = QFileInfo(QString::fromStdString(e->get_system_data()->system_file)).completeBaseName();
    if (machine.isEmpty()) machine = "ecat3";
    const QString suggested = QDir(folder).filePath(machine + "-" +
                              QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss") + ".png");

    QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save screenshot"), suggested, "PNG (*.png)");

    if (!file_name.isEmpty())
    {
        e->write_setup("Video", "screenshot_folder",
                       QDir::toNativeSeparators(QFileInfo(file_name).absolutePath()).toStdString());

        if (!shown.isNull()) {
            shown.save(file_name, "PNG");
        } else {
            std::vector<unsigned char> png;
            lodepng::encode(png, image, sx, sy);
            lodepng::save_file(png, file_name.toUtf8().constData());
        }

        //A replay writes the machine's pixels whatever the type: SCREEN has
        //no window to take the picture from

        e->record_verb_at(SCRIPT_CMD_SCREEN,
            std::vector<std::string>(1, QFileInfo(file_name).absoluteFilePath().toStdString()), taken_at);
    }
}


//------------------------- Video recording ----------------------------//

//The one named in the ini, the one next to the emulator, the one on the PATH
QString MainWindow::find_ffmpeg()
{
    const QString named = QString::fromStdString(e->read_setup("Video", "ffmpeg", ""));
    if (!named.isEmpty() && QFileInfo(named).isFile()) return named;
#ifdef Q_OS_WIN
    const QString exe = "ffmpeg.exe";
#else
    const QString exe = "ffmpeg";
#endif
    const QString local = QCoreApplication::applicationDirPath() + "/" + exe;
    if (QFileInfo(local).isFile()) return local;
    return QStandardPaths::findExecutable("ffmpeg");
}

void MainWindow::on_actionVideo_triggered()
{
    if (e == nullptr) return;
    if (e->video_recording()) video_stop();
    else if (e->video_state() != VideoRecorder::FINISHING) video_start();
    video_update_action();
}

void MainWindow::video_start()
{
    if (!e->loaded) return;

    QString ffmpeg = find_ffmpeg();
    if (ffmpeg.isEmpty()) {
        QMessageBox::information(this, tr("Video recording"),
            tr("Video is recorded by ffmpeg, which is not found next to the emulator or on the PATH. Please show where it is."));
        ffmpeg = QFileDialog::getOpenFileName(this, tr("Path to ffmpeg"));
        if (ffmpeg.isEmpty()) return;
        e->write_setup("Video", "ffmpeg", QDir::toNativeSeparators(ffmpeg).toStdString());
    }

    QString folder = QString::fromStdString(e->read_setup("Video", "video_folder", ""));
    if (folder.isEmpty()) folder = QString::fromStdString(e->work_path) + "/video";
    if (!QDir().mkpath(folder)) {
        QMessageBox::warning(this, tr("Error"), tr("Unable to create the folder ") + folder);
        return;
    }

    VideoSettings s;
    s.codec = VideoRecorder::codec_by_name(e->read_setup("Video", "video_codec", "h264"));
    const std::string fps_setting = e->read_setup("Video", "video_fps", "50");
    s.native = fps_setting == "native";
    if (!s.native) {
        try { s.fps_num = (unsigned int)std::stoi(fps_setting); } catch (const std::exception &) { s.fps_num = 50; }
        if (s.fps_num == 0 || s.fps_num > 240) s.fps_num = 50;
    }
    QString machine = QFileInfo(QString::fromStdString(e->get_system_data()->system_file)).completeBaseName();
    if (machine.isEmpty()) machine = "ecat3";
    const QString file = folder + "/" + machine + "-" +
                         QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss") +
                         VideoRecorder::extension(s.codec);
    s.file = QDir::toNativeSeparators(file).toStdString();
    s.ffmpeg = QDir::toNativeSeparators(ffmpeg).toStdString();
    //One per process: two emulators recording at once would read each other's
    s.log_file = QDir::toNativeSeparators(QDir::tempPath() + "/ecat3-ffmpeg-" +
                 QString::number(QCoreApplication::applicationPid()) + ".log").toStdString();

    std::string error;
    if (!e->start_video_recording(s, screenshot_as_shown, error)) {
        QMessageBox::warning(this, tr("Error"), QString::fromStdString(error));
        return;
    }
    video_active = true;

    if (screenshot_as_shown) {
#if defined(RENDERER_OPENGL)
        //Every frame the window draws, read back from what it has just drawn
        static_cast<GLWidget*>(screen)->setFrameHook([this](const uint8_t * rgba, int w, int h) {
            if (e != nullptr) e->video_frame(rgba, (unsigned int)w, (unsigned int)h, true);
        });
        screen->update();
#else
        if (video_frame_timer == nullptr) {
            video_frame_timer = new QTimer(this);
            connect(video_frame_timer, &QTimer::timeout, this, &MainWindow::video_send_shown_frame);
        }
        video_send_shown_frame();
        video_frame_timer->start(s.native ? 20 : 1000 / (int)s.fps_num);
#endif
    }

    if (video_timer == nullptr) {
        video_timer = new QTimer(this);
        connect(video_timer, &QTimer::timeout, this, &MainWindow::video_tick);
    }
    video_timer->start(250);
    statusBar()->showMessage(tr("Recording video to ") + file);
}

void MainWindow::video_send_shown_frame()
{
    if (e == nullptr || !e->video_recording()) return;
#if defined(RENDERER_QT)
    //The widget's own scaled image: the machine's pixels are not needed
    const QImage shown = picture_as_shown(std::vector<uint8_t>(), 0, 0);
#else
    unsigned int sx, sy;
    const std::vector<uint8_t> raw = e->grab_screen(&sx, &sy);
    const QImage shown = picture_as_shown(raw, sx, sy);
#endif
    if (shown.isNull()) return;
    const QImage rgba = shown.convertToFormat(QImage::Format_RGBA8888);
    std::vector<uint8_t> pixels((size_t)rgba.width() * rgba.height() * 4);
    for (int y = 0; y < rgba.height(); y++)
        memcpy(pixels.data() + (size_t)y * rgba.width() * 4, rgba.constScanLine(y), (size_t)rgba.width() * 4);
    e->video_frame(pixels.data(), (unsigned int)rgba.width(), (unsigned int)rgba.height());
}

void MainWindow::video_stop()
{
    e->stop_video_recording();
#if defined(RENDERER_OPENGL)
    static_cast<GLWidget*>(screen)->setFrameHook(nullptr);
#endif
    if (video_frame_timer != nullptr) video_frame_timer->stop();
    statusBar()->showMessage(tr("Finishing the video..."));
}

//The recording may also end without the button: another machine, a fault of
//ffmpeg. Says how it went once it has
void MainWindow::video_tick()
{
    if (e == nullptr) return;
    const VideoRecorder::State st = e->video_state();
    if (video_active && st != VideoRecorder::RECORDING) {
#if defined(RENDERER_OPENGL)
        static_cast<GLWidget*>(screen)->setFrameHook(nullptr);
#endif
        if (video_frame_timer != nullptr) video_frame_timer->stop();
    }
    if (st == VideoRecorder::FAILED) e->stop_video_recording();
    if (st == VideoRecorder::DONE || st == VideoRecorder::FAILED) {
        if (video_active) {
            video_active = false;
            if (video_timer != nullptr) video_timer->stop();
            const QString msg = QString::fromStdString(e->video_message());
            if (st == VideoRecorder::DONE)
                statusBar()->showMessage(tr("Video saved: ") + msg, 15000);
            else {
                statusBar()->clearMessage();
                QMessageBox::warning(this, tr("Video recording"), msg);
            }
        }
    }
    video_update_action();
}

void MainWindow::video_update_action()
{
    if (e == nullptr) return;
    const VideoRecorder::State st = e->video_state();
    const bool recording = st == VideoRecorder::RECORDING;
    ui->actionVideo->setIcon(QIcon(recording?":/icons/stop":":/icons/video"));
    ui->actionVideo->setText(recording?tr("Stop video recording"):tr("Record video"));
    ui->actionVideo->setToolTip(ui->actionVideo->text());
    ui->actionVideo->setEnabled(st != VideoRecorder::FINISHING);
}

void MainWindow::on_actionAbout_triggered()
{
    QDialog * about = new QDialog(this);

    Ui_About aboutUi;
    aboutUi.setupUi(about);

    aboutUi.title_label->setText(
        aboutUi.title_label->text()
            .replace("{$PROJECT_VERSION}", PROJECT_VERSION)
        );
    QString compilerInfo;
#if defined(_MSC_VER)
    compilerInfo = QString("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    compilerInfo = QString("Clang %1.%2.%3")
        .arg(__clang_major__)
        .arg(__clang_minor__)
        .arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    compilerInfo = QString("GCC %1.%2.%3")
        .arg(__GNUC__)
        .arg(__GNUC_MINOR__)
        .arg(__GNUC_PATCHLEVEL__);
#else
    compilerInfo = "Unknown";
#endif

    aboutUi.info_label->setText(
        aboutUi.info_label->text()
            .replace("{$QT_VERSION}", QT_VERSION_STR )
            .replace("{$RENDERER}", QString::fromStdString(renderer->get_name()))
            .replace("{$BUILD_ARCHITECTURE}", QSysInfo::buildCpuArchitecture())
            .replace("{$OS}", QSysInfo::productType())
            .replace("{$OS_VERSION}", QSysInfo::productVersion())
            .replace("{$CPU_ARCHITECTURE}", QSysInfo::currentCpuArchitecture())
            .replace("{$COMPILER}", compilerInfo)
#ifdef USE_SDL_AUDIO
            .replace("{$AUDIO_DRIVER}", "SDL2")
#else
            .replace("{$AUDIO_DRIVER}", "miniaudio")
#endif
        );

    about->exec();
}


void MainWindow::on_actionTape_triggered()
{
    //Магнитофон у машины один, и окно у него одно: повторное нажатие кнопки
    //поднимает уже открытое, а не заводит второе. Окно закрывается по
    //WA_DeleteOnClose, так что после закрытия findChild его уже не находит и
    //кнопка открывает новое - как у окна клавиатуры рядом
    TapeRecorderWindow * w = findChild<TapeRecorderWindow*>();
    if (w != nullptr) {
        w->raise();
        w->activateWindow();
        return;
    }

    //By class, the way the tool bar button is put up: the menu item is there
    //for every machine, and most of them have no device named "tape" - looked
    //up by that name, its absence was an exception leaving the slot
    if (!e->loaded) return;
    std::vector<ComputerDevice*> tape_devices = e->dm->find_devices_by_class("tape");
    TapeRecorder * tape = tape_devices.empty() ? nullptr : dynamic_cast<TapeRecorder*>(tape_devices[0]);
    if (tape == nullptr) return;

    w = new TapeRecorderWindow(this, e, tape);
    w->setAttribute(Qt::WA_DeleteOnClose);
    remember_geometry(w, "");
    w->show();
}

void MainWindow::on_actionKeyboard_triggered()
{
#ifdef HAVE_QT_SVG
    KeyboardWindow * w = findChild<KeyboardWindow*>();
    if (w != nullptr) {
        w->raise();
        w->activateWindow();
        return;
    }

    w = new KeyboardWindow(this, e);
    if (!w->valid()) {
        //A drawing that cannot be used is worse than none: say so instead of
        //opening an empty window
        delete w;
        QMessageBox::warning(this, tr("Keyboard"), tr("This machine has no usable keyboard picture"));
        return;
    }
    w->setAttribute(Qt::WA_DeleteOnClose);
    remember_geometry(w, "");
    w->show();
#endif
}

//---------------------------- Action recording ----------------------------//

QString MainWindow::format_mmss(uint64_t ms)
{
    return QString("%1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0'));
}

QString MainWindow::rec_machine_string() const
{
    //The form the ini file and MACHINE use: relative to computers/ when the
    //configuration lives there, absolute otherwise
    QString file = QDir::cleanPath(QString::fromStdString(e->get_system_data()->system_file));
    QString base = QDir::cleanPath(QString::fromStdString(e->work_path));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (file.startsWith(base + "/", cs)) return file.mid(base.length() + 1);
    return file;
}

bool MainWindow::rec_machine_matches(const std::string &machine) const
{
    if (machine.empty() || !e->loaded) return true;
    QString wanted = QFileInfo(resolve_startup_path(QString::fromStdString(machine))).canonicalFilePath();
    QString current = QFileInfo(QString::fromStdString(e->get_system_data()->system_file)).canonicalFilePath();
    if (wanted.isEmpty() || current.isEmpty()) return false;
#ifdef Q_OS_WIN
    return QString::compare(wanted, current, Qt::CaseInsensitive) == 0;
#else
    return wanted == current;
#endif
}

void MainWindow::rec_refresh_total()
{
    ScriptEngine * s = e->script_engine();
    rec_total_ms = script_duration_ms(s->get_commands(), 0, s->size());
}

void MainWindow::rec_update_ui()
{
#ifdef ENABLE_MCP
    if (mcp_mode)
    {
        //The external driver owns the command buffer. A human pressing Rewind
        //would call truncate() / seek() under its feet
        QAction * rec_actions[] = {ui->actionRecOpen, ui->actionRecSave, ui->actionRecord,
                                   ui->actionRecPlay, ui->actionRecRewind, ui->actionRecStop};
        for (size_t i = 0; i < sizeof(rec_actions) / sizeof(rec_actions[0]); i++)
            rec_actions[i]->setEnabled(false);
        rec_label->setVisible(false);
        return;
    }
#endif

    ScriptEngine * s = e->script_engine();
    const bool empty = (s->size() == 0);
    const size_t pc = s->get_pc();
    const bool idle = (rec_state == RecIdle);
    const bool recording = (rec_state == RecRecording);
    const bool playing = (rec_state == RecPlaying);

    ui->actionRecOpen->setEnabled(idle);
    ui->actionRecSave->setEnabled(!empty && !recording);
    ui->actionRecord->setEnabled(!playing);
    ui->actionRecPlay->setEnabled(!empty && !recording);
    ui->actionRecRewind->setEnabled(!empty && pc > 0 && (idle || rec_state == RecPaused));
    ui->actionRecStop->setEnabled(!idle);

    ui->actionRecord->setIcon(QIcon(recording?":/icons/pause":":/icons/record"));
    ui->actionRecord->setText(recording?tr("Pause recording"):tr("Start recording"));
    ui->actionRecord->setToolTip(ui->actionRecord->text());

    ui->actionRecPlay->setIcon(QIcon(playing?":/icons/pause":":/icons/play"));
    ui->actionRecPlay->setText(playing?tr("Pause playback"):tr("Play recording"));
    ui->actionRecPlay->setToolTip(ui->actionRecPlay->text());

    rec_label->setVisible(rec_ui_shown);
}

void MainWindow::rec_sync_option_combos(size_t from, size_t to)
{
    //A replayed COMMAND dev.option(id, value) changes the device but not the
    //dropdown of the tool bar, so the dropdown follows the commands executed
    //since the last poll. Signals are blocked: the change is neither written
    //to the ini file nor recorded again
    const std::vector<ScriptCommand> &commands = e->script_engine()->get_commands();
    for (size_t i = from; i < to && i < commands.size(); i++)
    {
        const ScriptCommand &c = commands[i];
        if (c.verb != SCRIPT_CMD_COMMAND || c.member != "option") continue;

        std::vector<std::string> p = split_params(c.params);
        if (p.size() < 2) continue;

        unsigned int option_id, value_id;
        try {
            option_id = parse_numeric_value(p[0]);
            value_id = parse_numeric_value(p[1]);
        } catch (...) {
            continue;
        }

        for (int j = 0; j < option_combos.size(); j++)
        {
            const OptionCombo &oc = option_combos[j];
            if (oc.device != c.device || oc.option != option_id) continue;
            if (oc.toggle != nullptr) {
                QSignalBlocker blocker(oc.toggle);
                oc.toggle->setChecked(value_id != 0);
                continue;
            }
            int index = oc.combo->findData(value_id);
            if (index >= 0) {
                QSignalBlocker blocker(oc.combo);
                oc.combo->setCurrentIndex(index);
            }
        }
    }
}

void MainWindow::rec_tick()
{
    //The machine stopped by itself. The core keeps the reason and tells no
    //one; before, the window simply froze, and a command line script waited
    //for its EXIT until the runner gave up
    bool fatal = false;
    const std::string why = e->machine_error(&fatal);
    //Under MCP it is left where it is: the client learns it from the command
    //that stalls on it (McpSession), and a box would block this thread
#ifdef ENABLE_MCP
    const bool under_mcp = mcp_mode;
#else
    const bool under_mcp = false;
#endif
    if (!why.empty() && !under_mcp) {
        e->clear_machine_error();
        if (rec_cmdline) {
            //A device that stops the machine is part of what a script may
            //test, and any output on stderr fails a test: only a dead
            //emulation thread is reported, and the run ends with it
            if (fatal) {
                std::cerr << "Emulation stopped: " << strip_message_context(why) << std::endl;
                rec_timer->stop();
                rec_exit_code = 3;
                close();
                return;
            }
        } else {
            QMessageBox::warning(this, fatal ? tr("Emulation stopped") : tr("The machine stopped"),
                                 translateResultMessage(why));
        }
    }

    //The processor is stopped and resumed from the debug windows and from the
    //scripts as well, so the tool bar button is refreshed by polling
    update_cpu_state_action();

    ScriptEngine * s = e->script_engine();
    const uint64_t now = e->clock_now();

    if (rec_state == RecPlaying)
    {
        size_t pc = s->get_pc();
        if (pc > rec_seen_pc) {
            rec_sync_option_combos(rec_seen_pc, pc);
            rec_seen_pc = pc;
        }
        if (s->is_finished()) {
            if (rec_cmdline && s->is_exit_requested()) {
                rec_timer->stop();
                //The engine dies with the emulator inside closeEvent()
                rec_exit_code = s->get_exit_code();
                close();
                return;
            }
            rec_cmdline = false;
            rec_stop_all();
        }
    }

    if (!rec_ui_shown) return;

    if (rec_state == RecRecording)
    {
        rec_refresh_total();
        rec_label->setText(format_mmss(rec_total_ms + e->script_recorder()->live_ms(now)));
    }
    else
    {
        uint64_t position = (rec_state == RecIdle)
            ? script_duration_ms(s->get_commands(), 0, s->get_pc())
            : s->get_position_ms(now);
        if (position > rec_total_ms) position = rec_total_ms;
        rec_label->setText(format_mmss(position) + " / " + format_mmss(rec_total_ms));
    }
}

void MainWindow::rec_stop_all()
{
    ScriptEngine * s = e->script_engine();
    ScriptRecorder * r = e->script_recorder();
    const uint64_t now = e->clock_now();

    switch (rec_state)
    {
        case RecRecording:
            r->end(now);
            s->seek(s->size());     //The next Play starts from the beginning
            break;

        case RecPlaying:
        case RecPaused:
            s->stop();              //The pointer stays where the replay stopped
            r->mark(now);
            break;

        default:
            break;
    }

    rec_state = RecIdle;
    rec_refresh_total();
    rec_update_ui();
}

bool MainWindow::rec_save()
{
    ScriptEngine * s = e->script_engine();
    if (s->size() == 0) return false;

    QString suggested = rec_file.isEmpty()?QDir(last_path).filePath("recording.ecat"):rec_file;
    QString file_name = QFileDialog::getSaveFileName(this, tr("Save recording"), suggested, tr("eCat scripts (*.ecat)"));
    if (file_name.isEmpty()) return false;
    if (QFileInfo(file_name).suffix().isEmpty()) file_name += ".ecat";

    emulator::Result res = write_script_file(file_name.toStdString(), s->get_commands());
    if (!res) {
        QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
        return false;
    }

    //From now on the log and relative screenshot names go next to the file
    e->set_script_file(file_name.toStdString());
    rec_file = file_name;
    return true;
}

void MainWindow::on_actionRecOpen_triggered()
{
    if (rec_state != RecIdle) return;

    ScriptEngine * s = e->script_engine();
    if (s->size() > 0)
    {
        QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Open recording"),
            tr("Save the current recording?"), QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) return;
        if (reply == QMessageBox::Yes && !rec_save()) return;
    }

    QString dir = rec_file.isEmpty()?last_path:QFileInfo(rec_file).absolutePath();
    QString file_name = QFileDialog::getOpenFileName(this, tr("Open recording"), dir, tr("eCat scripts (*.ecat);;All files (*.*)"));
    if (file_name.isEmpty()) return;

    emulator::Result res = e->load_script(file_name.toStdString());
    if (!res) {
        QMessageBox::warning(this, tr("Error"), translateResultMessage(res.message));
        return;
    }

    const std::vector<std::string> &errors = s->get_errors();
    if (!errors.empty()) {
        QStringList lines;
        for (size_t i = 0; i < errors.size(); i++) lines << translateResultMessage(errors[i]);
        QMessageBox::warning(this, tr("Open recording"), tr("Some lines were skipped:") + "\n" + lines.join("\n"));
    }

    e->script_recorder()->invalidate();
    rec_file = file_name;
    rec_cmdline = false;
    rec_ui_shown = true;
    rec_refresh_total();
    rec_update_ui();
}

void MainWindow::on_actionRecSave_triggered()
{
    if (rec_state == RecRecording) return;
    rec_save();
}

void MainWindow::on_actionRecord_triggered()
{
    if (rec_state == RecRecording) {
        rec_stop_all();
        return;
    }
    if (rec_state == RecPlaying || !e->loaded) return;

    ScriptEngine * s = e->script_engine();
    ScriptRecorder * r = e->script_recorder();

    if (rec_state == RecPaused) {
        s->stop();
        r->mark(e->clock_now());
        rec_state = RecIdle;
    }

    if (s->size() > 0)
    {
        if (!rec_machine_matches(s->get_machine()))
        {
            QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Start recording"),
                tr("The recording was made for another machine. Discard it and start a new one?"),
                QMessageBox::Yes|QMessageBox::Cancel);
            if (reply != QMessageBox::Yes) { rec_update_ui(); return; }
            s->clear();
            r->invalidate();
            rec_file.clear();
        }
        else
        {
            //Whatever was not replayed yet is dropped and re-recorded
            s->truncate(s->get_pc());
        }
    }

    const bool fresh = s->size() == 0;
    r->begin(e->clock_now(), e->ticks_per_ms(), rec_machine_string().toStdString());
    //A replay loads the machine with the configuration's disks, so a new
    //recording starts by putting in what the drives hold now
    if (fresh) e->record_disk_changes();
    rec_cmdline = false;
    rec_state = RecRecording;
    rec_ui_shown = true;
    rec_refresh_total();
    rec_update_ui();
}

void MainWindow::on_actionRecPlay_triggered()
{
    ScriptEngine * s = e->script_engine();

    if (rec_state == RecPlaying) {
        s->pause();
        rec_state = RecPaused;
        rec_update_ui();
        return;
    }
    if (rec_state == RecRecording || s->size() == 0) return;

    //A recording made for another machine loads that machine first, the way
    //the command line does
    std::string machine = s->get_machine();
    if (!machine.empty() && !rec_machine_matches(machine))
    {
        QString path = resolve_startup_path(QString::fromStdString(machine));
        if (!QFileInfo::exists(path)) {
            QMessageBox::warning(this, tr("Error"), tr("Configuration file is not found: ") + path);
            return;
        }
        no_disk_restore = true;
        load_config(path, false, false);
        if (!e->loaded) return;
    }

    s->resume(e->clock_now());
    rec_seen_pc = s->get_pc();
    rec_state = RecPlaying;
    rec_ui_shown = true;
    rec_refresh_total();
    rec_update_ui();
}

void MainWindow::on_actionRecRewind_triggered()
{
    if (rec_state == RecRecording || rec_state == RecPlaying) return;

    ScriptEngine * s = e->script_engine();
    if (rec_state == RecPaused) s->stop();
    s->seek(0);
    e->script_recorder()->invalidate();
    rec_state = RecIdle;
    rec_update_ui();
}

void MainWindow::on_actionRecStop_triggered()
{
    rec_stop_all();
}

void MainWindow::on_actionRecPanel_toggled(bool checked)
{
    //The menu keeps working while the block is hidden
    if (rec_panel_separator != nullptr) rec_panel_separator->setVisible(checked);
    if (rec_panel_widget != nullptr) rec_panel_widget->setVisible(checked);
    if (e != nullptr) e->write_setup("Video", "recording_panel", checked?"1":"0");
}

//Turning it off keeps the entries: they are neither applied nor updated until
//it is on again
void MainWindow::on_actionRememberDisks_toggled(bool checked)
{
    if (e != nullptr) e->write_setup("Core", "remember_disks", checked?"1":"0");
}

void MainWindow::on_actionForgetDisks_triggered()
{
    if (e == nullptr) return;
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Remembered disks"),
        tr("Forget the disks chosen for every machine? The disks in the drives now stay where they are."),
        QMessageBox::Yes|QMessageBox::Cancel);
    if (reply == QMessageBox::Yes) e->forget_all_disks();
}


#ifdef ENABLE_MCP

//---------------------------- MCP server ----------------------------------//

void MainWindow::enable_mcp(bool trace)
{
    mcp_mode = true;
    //A session driven from outside must leave the working tree as it found it
    e->set_settings_readonly(true);
    //The recording transport must not touch the command buffer any more
    rec_ui_shown = false;
    rec_update_ui();

    mcp_bridge = new McpBridge(this, trace);
    mcp_bridge->start();
}

void MainWindow::mcp_show_window()
{
    //The renderer is attached to the widget, so the window has to exist before
    //a machine is brought up. It is only shown when a machine is asked for
    if (!isVisible()) show();
}

QString MainWindow::mcp_current_machine() const
{
    return cur_config;
}

QString MainWindow::mcp_load_config(const QString &file_name)
{
    mcp_show_window();

    mcp_load_error.clear();
    //The client drives the machine itself, the script of an extension would
    //run under its feet
    load_config(resolve_startup_path(file_name), false, false);

    if (!mcp_load_error.isEmpty()) return mcp_load_error;
    if (!e->loaded) return tr("The configuration did not load.");

    if (!mcp_screen_menu)
    {
        CreateScreenMenu();
        mcp_screen_menu = true;
    }
    return QString();
}

#endif
