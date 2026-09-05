// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Main window source

#include <QDebug>
#include <QDir>
#include <QFontDatabase>
#include <QEvent>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QFileDialog>
#include <QWidgetAction>
#include <QPushButton>
#include <QActionGroup>
#include <QSignalBlocker>
#include <QBoxLayout>
// #include <QOverload>
#include <QMessageBox>

#include "dialogs/genericdbgwnd.h"
#include "dialogs/i8255window.h"
#include "mainwindow.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"
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
#include "emulator/devices/common/tape.h"
#include "emulator/script/script_parser.h"
#include "dialogs/taperecorder.h"

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

#if defined(__linux__)
    if (std::filesystem::exists(QString(current_path + "/computers").toStdString())) {
        emulator_root = current_path;
    } else {
        emulator_root = app_path.left(app_path.lastIndexOf('/')) + "/share/ecat";
    }

    ini_path = QString(getenv("HOME")) + "/.config";
    ini_file = ini_path + "/ecat.ini";
    if (!std::filesystem::exists(ini_file.toStdString())) {
        if (std::filesystem::exists(QString(emulator_root + "/ecat.ini").toStdString())) {
            std::filesystem::copy_file(QString(emulator_root + "/ecat.ini").toStdString(), ini_file.toStdString());
        } else {
            std::filesystem::copy_file(QString(current_path + "/ecat.ini").toStdString(), ini_file.toStdString());
        }
    }
#elif defined(__APPLE__)
    if (std::filesystem::exists(QString(current_path + "/computers").toStdString())) {
        emulator_root = current_path;
    } else {
        emulator_root = app_path.left(app_path.lastIndexOf('/')) + "/Resources";
    }

    ini_path = QString(getenv("HOME"));
    ini_file = ini_path + "/.ecat.ini";
    if (!std::filesystem::exists(ini_file.toStdString())) {
        if (std::filesystem::exists(QString(emulator_root + "/ecat.ini").toStdString())) {
            std::filesystem::copy_file(QString(emulator_root + "/ecat.ini").toStdString(), ini_file.toStdString());
        } else {
            std::filesystem::copy_file(QString(current_path + "/ecat.ini").toStdString(), ini_file.toStdString());
        }
    }
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
#else
#error "Unknown platform"
#endif

    qDebug() << "emulator_root: " << emulator_root ;

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

    add_languages();

#ifdef SDL_SEPARATE_WINDOW
    resize(500,100);
#endif

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

    // Signal/slot connections removed — using direct calls to Emulator methods

    QString file_to_load = QString::fromStdString(e->read_setup("Startup", "default", ""));

    last_path = QString::fromStdString(e->read_setup("Startup", "last_path", software_path.toStdString()));

    QString sound_volume = QString::fromStdString(e->read_setup("Sound", "volume", "50"));
    volume->setValue(sound_volume.toInt());

    QString muted = QString::fromStdString(e->read_setup("Sound", "muted", "0"));
    mute->setChecked(muted.toInt() == 1);

    //The recording block of the tool bar can be hidden from the Display menu
    ui->actionRecPanel->setChecked(e->read_setup("Video", "recording_panel", "1") != "0");

    //A configuration given on the command line wins over the one saved in the ini
    first_config = cmdline_config.isEmpty()
        ? (work_path + file_to_load)
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

        load_config(first_config, false);
        CreateScreenMenu();

        if (!script_file.isEmpty()) start_script();
    }
}

void MainWindow::start_script()
{
    if (!e->loaded) return;

    //A script from the command line runs through the same controls as a
    //recording, the only difference being that its EXIT closes the window
    e->start_script();
    rec_cmdline = true;
    rec_ui_shown = true;
    rec_state = RecPlaying;
    rec_seen_pc = 0;
    rec_refresh_total();
    rec_update_ui();
}

bool MainWindow::switch_language(const QString & lang, bool init)
{
    if (translator.load(":/i18n/" + lang)) {
        qApp->installTranslator(&translator);

        QString t = QString(":/i18n/qtbase_%1.qm").arg(lang.split("_")[0]);
        if (qtTranslator.load(t)) {
            qApp->installTranslator(&qtTranslator);
        }
        if (!init) {
            ui->retranslateUi(this);
            CreateScreenMenu();
            rec_update_ui();
            m_settings->set("interface", "language", lang.toStdString());
            m_settings->save();
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
    fdd_menu[n]->addAction(a1);
    fdd_menu[n]->addSeparator();
    fdd_menu[n]->addAction(a2);
    fdd_menu[n]->addAction(a3);
    fdd_menu[n]->addAction(a4);
    fdd_menu[n]->addAction(a5);

    fdd_button[n] = new QToolButton();
    fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    fdd_button[n]->setMenu(fdd_menu[n]);
    fdd_button[n]->setPopupMode(QToolButton::MenuButtonPopup);
    fdd_button[n]->setFocusPolicy(Qt::NoFocus);

    connect(fdd_button[n], &QToolButton::clicked, this, [this, n](){fdd_open(n);});

    ui->toolBar->insertWidget(ui->actionDebugger, fdd_button[n] );
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::CreateScreenMenu()
{
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
        a->setChecked(e->get_scale() == i);
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
        ui->menuFiltering->clear();
        ui->menuFiltering->setDisabled(true);
    #endif
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
        for (int i=0; i < fdds_found; i++) {
            delete fdd_button[i];
            delete fdd_menu[i];
        }
        fdds.clear();
    }
    if (fdd_timer != nullptr) fdd_timer->stop();

    if (tape_action != nullptr) {
        ui->toolBar->removeAction(tape_action);
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
    fdds_found = fdd_devices.size();

    for (int i=0; i < fdds_found; i++) {
        FDD * fdd = dynamic_cast<FDD*>(fdd_devices[i]);
        fdds.push_back(fdd);
        CreateFDDMenu(i);
        if (fdd->get_loaded())
        {
            fdd_menu[i]->actions().at(0)->setText(QString::fromStdString(fdd->file_name));
            fdd_button[i]->setIcon(QIcon(":/icons/floppy_mount"));
            if (fdd->is_protected())
                fdd_button[i]->setIcon(QIcon(":/icons/floppy_locked"));
        }

    }
    if (fdds_found > 0) {
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

    // Device options
    bool has_hw_buttons = (fdds_found > 0 || tape_devices.size() != 0);
    bool options_separator_added = false;

    SystemData * sd = e->get_system_data();
    QFileInfo opt_fi(QString::fromStdString(sd->system_file));
    QString config_key = opt_fi.baseName();

    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * dev = e->dm->get_device(i)->device.get();
        DeviceOptions options = dev->get_device_options();
        for (size_t j = 0; j < options.size(); j++) {
            const DeviceOption & opt = options[j];
            if (opt.type == DEVICE_OPTION_DROPDOWN && !opt.values.empty()) {
                if (has_hw_buttons && !options_separator_added) {
                    QAction * sep = ui->toolBar->insertSeparator(ui->actionDebugger);
                    option_toolbar_actions.append(sep);
                    options_separator_added = true;
                }
                buttons_added++;

                QString option_tooltip = QCoreApplication::translate("DeviceOptions", opt.title.c_str());

                QAction * icon_action = nullptr;
                if (!opt.icon.empty()) {
                    QString icon_path = QString::fromStdString(find_file_location(sd, opt.icon));
                    if (!icon_path.isEmpty()) {
                        QIcon icon(icon_path);
                        icon.addPixmap(QPixmap(icon_path), QIcon::Disabled);
                        icon_action = new QAction(icon, "", this);
                        icon_action->setToolTip(option_tooltip);
                        ui->toolBar->insertAction(ui->actionDebugger, icon_action);
                        option_toolbar_actions.append(icon_action);
                    }
                }

                QComboBox * combo = new QComboBox();
                combo->setFocusPolicy(Qt::NoFocus);
                combo->setToolTip(option_tooltip);

                std::string settings_key = config_key.toStdString() + "_" + dev->name + "_" + std::to_string(opt.id);
                QString saved = QString::fromStdString(e->read_setup("DeviceOptions", settings_key, ""));

                int selected_index = 0;
                for (size_t v = 0; v < opt.values.size(); v++) {
                    combo->addItem(
                        QCoreApplication::translate("DeviceOptions", opt.values[v].title.c_str()),
                        opt.values[v].id
                    );
                    if (!saved.isEmpty() && opt.values[v].id == saved.toUInt()) {
                        selected_index = static_cast<int>(v);
                    }
                }
                combo->setCurrentIndex(selected_index);

                unsigned option_id = opt.id;
                std::string device_name = dev->name;
                connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                    [this, combo, dev, option_id, config_key, device_name](int index) {
                        unsigned value_id = combo->itemData(index).toUInt();
                        dev->set_device_option(option_id, value_id);
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
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        // qDebug() << "Key pressed: scan " << event->nativeScanCode() << "virtual" << event->nativeVirtualKey() << "key" << Qt::hex << event->key();
        e->key_event(event->key(), event->modifiers(), true);

        if (event->key() == EmuKey::Cancel) {
            //Pause/Break resets the machine, see Emulator::key_event(); the
            //key has no script name, so the reset itself is recorded
            std::vector<std::string> args;
            args.push_back((event->modifiers() & Qt::AltModifier)?"cold":"soft");
            e->record_verb(SCRIPT_CMD_RESET, args);
        } else {
            e->record_key(static_cast<unsigned int>(event->key()), event->nativeScanCode(), true);
        }
    }
}

void MainWindow::keyReleaseEvent( QKeyEvent *event )
{
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key released:" << event->nativeScanCode() << event->nativeVirtualKey() << event->key();
        e->key_event(event->key(), event->modifiers(), false);
        e->record_key(static_cast<unsigned int>(event->key()), event->nativeScanCode(), false);
    }
}

void MainWindow::resizeEvent(QResizeEvent * event)
{
    //e->resize_screen();
}

void MainWindow::paintEvent(QPaintEvent * event)
{
    //QMainWindow::paintEvent(event);
    e->resize_screen();
}

void MainWindow::on_action_Cold_restart_triggered()
{
    e->reset(true);
    e->record_verb(SCRIPT_CMD_RESET, std::vector<std::string>(1, "cold"));
}


void MainWindow::on_action_Soft_restart_triggered()
{
    e->reset(false);
    e->record_verb(SCRIPT_CMD_RESET, std::vector<std::string>(1, "soft"));
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

void MainWindow::load_config(QString file_name, bool set_default)
{
    if (e->loaded)
    {
        if (fdd_timer != nullptr) fdd_timer->stop();

        //Switching the machine invalidates every device the script addresses.
        //The buffer is kept: a replay may be the reason for the switch
        rec_stop_all();
        e->script_recorder()->invalidate();
        e->stop_script();

        e->stop_emulation();
    }

    emulator::Result res = e->load_config(file_name.toStdString());
    if (!res) {
#ifdef ENABLE_MCP
        //A modal box here would block the GUI thread that the MCP client is
        //waiting on, so the error is reported through the protocol instead
        if (mcp_mode) { mcp_load_error = translateResultMessage(res.message); return; }
#endif
        QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
        return;
    }

    cur_config = file_name;

    set_title();

    CreateDevicesMenu();
    UpdateToolbar();

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
            QString new_file = file_name.right(file_name.length() - static_cast<int>(e->work_path.length()));
            e->write_setup("Startup", "default", new_file.toStdString());
            qDebug() << new_file;
        }
    } else {
        qWarning() << "ui->screen->winId() is null!";
    }
}


void MainWindow::on_actionOpen_triggered()
{
    SystemData * sd = e->get_system_data();
    QString file_name = QFileDialog::getOpenFileName(this, tr("Load a file"), last_path, QString::fromStdString(sd->allowed_files));


    if (!file_name.isEmpty()) {
        QFileInfo fi(file_name);
        last_path = fi.absolutePath();
        e->write_setup("Startup", "last_path", last_path.toStdString());

        emulator::Result res = HandleExternalFile(e, file_name.toStdString());
        if (!res) {
            QMessageBox::warning(this, tr("Error"), translateResultMessage(res.message));
        }
    }
}

void MainWindow::on_actionDebugger_triggered()
{
    CPU * cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu"));
    DebugWndCreateFunc * f = DWM->get_create_func(cpu->type);
    if (f != nullptr)
    {
            GenericDbgWnd * w = f(this, e, cpu);
            w->setAttribute(Qt::WA_DeleteOnClose);
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

void MainWindow::closeEvent (QCloseEvent *event)
{
    fdds_found = 0; // to prevent crashing on buttons update

    // The timers must not fire once the emulator is gone
    if (fdd_timer != nullptr) fdd_timer->stop();
    if (rec_timer != nullptr) rec_timer->stop();

    // Close all debug windows before destroying the emulator,
    // so their closeEvent handlers can safely access devices
    QList<GenericDbgWnd*> dbgWindows = findChildren<GenericDbgWnd*>();
    for (auto *w : dbgWindows)
        w->close();

    e->stop_emulation();
    delete e;

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
            emulator::Result res = fdds[n]->load_image(file_name.toStdString());
            if (!res) {
                QMessageBox::critical(this, tr("Error"), translateResultMessage(res.message));
            } else {
                e->record_command(fdds[n]->name, "load", format_script_arg(fi.absoluteFilePath().toStdString()));
            }
            last_path = fi.absolutePath();
            e->write_setup("Startup", "last_path", last_path.toStdString());
        }
    }
}

void MainWindow::fdd_eject(unsigned int n)
{
    fdd_menu[n]->actions().at(0)->setText(MainWindow::tr("<Not loaded>"));
    fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    fdds[n]->unload();
    e->record_command(fdds[n]->name, "eject", "");
}

void MainWindow::fdd_wp(unsigned int n)
{
    fdds[n]->change_protection();
    e->record_command(fdds[n]->name, "protect", fdds[n]->is_protected()?"1":"0");
    if (fdds[n]->get_loaded()) {
        if (fdds[n]->is_protected()) {
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_locked"));
        } else {
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_mount"));
        }
    }
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
            if (reply == QMessageBox::Yes)
            {
                save_res = fdds[n]->save_image(std_file_name);
            } else
            if (reply == QMessageBox::No)
            {
                QString backup_name = file_name + ".bak";
                bool result = QFile::rename(file_name, backup_name);
                if (result)
                    save_res = fdds[n]->save_image(std_file_name);
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
            e->write_setup("Startup", "last_path", last_path.toStdString());
        }
    }

}

void MainWindow::update_fdds()
{
    // TODO: this event may happen after exiting or stopping emulator
    for (unsigned int i=0; i<fdds_found; i++)
    {
        //if (fdc->get_busy() && fdc->get_selected_drive()==i) {
        if (fdds[i]->is_led_on()) {
            fdd_blinker = !fdd_blinker;
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
}

void MainWindow::on_actionScreenshot_triggered()
{
    unsigned int sx, sy;
    e->get_screen_constraints(&sx, &sy);
    std::vector<uint8_t> image = renderer->get_screenshot();

    //The image is taken before the dialog, so the recording refers to that moment
    uint64_t taken_at = e->clock_now();

    QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save screenshot"), QString::fromStdString(e->work_path), "PNG (*.png)");

    if (!file_name.isEmpty())
    {
        unsigned error;
        std::vector<unsigned char> png;
        error = lodepng::encode(png, image, sx, sy);
        lodepng::save_file(png, file_name.toUtf8().constData());

        e->record_verb_at(SCRIPT_CMD_SCREEN,
            std::vector<std::string>(1, QFileInfo(file_name).absoluteFilePath().toStdString()), taken_at);
    }
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
    TapeRecorder * tape = dynamic_cast<TapeRecorder*>(e->dm->get_device_by_name("tape"));

    if (tape != nullptr) {
        TapeRecorderWindow * w = new TapeRecorderWindow(this, e, tape);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
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

    r->begin(e->clock_now(), e->ticks_per_ms(), rec_machine_string().toStdString());
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
        load_config(path, false);
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
    load_config(resolve_startup_path(file_name), false);

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
