#include <QDir>
#include <QFontDatabase>
#include <QEvent>
#include <QSlider>
#include <QFileDialog>
#include <QWidgetAction>
#include <QPushButton>
#include <QActionGroup>

#include "dialogs/i8255window.h"
#include "mainwindow.h"
#include "emulator/utils.h"
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
#include "dialogs/taperecorder.h"

#include "libs/lodepng/lodepng.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fdd_timer(nullptr)
    , fdds_found(0)
    , fdd_blinker(false)
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

    m_settings = new QSettings(ini_file, QSettings::IniFormat);

    QString ini_lang = m_settings->value("interface/language", "").toString();

    if (ini_lang.length() == 0) {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString &locale : uiLanguages) {
            const QString baseName = QLocale(locale).name().toLower();
            switch_language(baseName, true);
            break;
        }
    } else {
        switch_language(ini_lang, true);
    }

    ui->setupUi(this);

    ui->screen->setUpdatesEnabled(false);

    add_languages();

#ifdef SDL_SEPARATE_WINDOW
    resize(500,100);
#endif

    memset(&fdds, 0, sizeof(fdds));

    QIcon * icon = new QIcon();
    icon->addFile(QString::fromUtf8(":/icons/sound"), QSize(), QIcon::Normal, QIcon::Off);
    icon->addFile(QString::fromUtf8(":/icons/sound-mute"), QSize(), QIcon::Normal, QIcon::On);

    mute = new QToolButton(this);
    mute->setFocusPolicy(Qt::NoFocus);
    mute->setCheckable(true);
    mute->setIcon(*icon);
    mute->setStyleSheet(
                            "QToolButton { /* all types of tool button */"
                            "border: 1px solid #8f8f91;"
                            "border-radius: 2px;"
                            "}"
        );
    statusBar()->addPermanentWidget(mute, 0);

    connect(mute, SIGNAL(toggled(bool)), this, SLOT(set_mute(bool)));


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

    connect(volume, SIGNAL(valueChanged(int)), this, SLOT(set_volume(int)));

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
    DWM->register_debug_window("taperecorder", &CreateTapeWindow);

    e = new Emulator(work_path, data_path, software_path, ini_file);

    connect(this, &MainWindow::send_a_key,  e, &Emulator::key_event);
    connect(this, &MainWindow::send_volume, e, &Emulator::set_volume);
    connect(this, &MainWindow::send_muted,  e, &Emulator::set_muted);
    connect(this, &MainWindow::send_reset,  e, &Emulator::reset);
    connect(this, &MainWindow::send_resize, e, &Emulator::resize_screen);
    connect(this, &MainWindow::send_stop,   e, &Emulator::stop_emulation, Qt::QueuedConnection);

    QString file_to_load = e->read_setup("Startup", "default", "");

    last_path = e->read_setup("Startup", "last_path", software_path);

    QString sound_volume = e->read_setup("Sound", "volume", "50");
    volume->setValue(sound_volume.toInt());

    QString muted = e->read_setup("Sound", "muted", "0");
    mute->setChecked(muted.toInt() == 1);

    first_config = work_path + file_to_load;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (first_show) {
        // We use this trick to ensure that all interface elements already have their final dimensions (especially on Linux).
        load_config(first_config, false);
        CreateScreenMenu();
        first_show = false;
    }
}

void MainWindow::switch_language(const QString & lang, bool init)
{
    if (translator.load(":/i18n/" + lang)) {
        qApp->installTranslator(&translator);

        QString t = QString(":/i18n/qtbase_%1.qm").arg(lang.split("_")[0]);
        if (qtTranslator.load(t)) {
            qApp->installTranslator(&qtTranslator);
        }
        if (!init) {
            ui->retranslateUi(this);
            // init_controls();
            m_settings->setValue("interface/language", lang);
        }
    } else {
        QMessageBox::warning(this, MainWindow::tr("Error"), MainWindow::tr("Failed to load language file for: ") + lang);
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

    for (unsigned int i=1; i <= 5; i++)
    {
        QAction * a = ui->menuScale->addAction(
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
        Emulator::tr("Screen 4:3"),
        [this]{e->set_ratio(SCREEN_RATIO_43);}
        );
    a1->setActionGroup(ratio_group);
    a1->setCheckable(true);
    a1->setChecked(e->get_ratio() == SCREEN_RATIO_43);
    QAction * a2 = ui->menuScreen_ratio->addAction(
        Emulator::tr("Square pixels"),
        [this]{e->set_ratio(SCREEN_RATIO_SQ);}
        );
    a2->setActionGroup(ratio_group);
    a2->setCheckable(true);
    a2->setChecked(e->get_ratio() == SCREEN_RATIO_SQ);
    QAction * a3 = ui->menuScreen_ratio->addAction(
        Emulator::tr("Square screen"),
        [this]{e->set_ratio(SCREEN_RATIO_11);}
        );
    a3->setActionGroup(ratio_group);
    a3->setCheckable(true);
    a3->setChecked(e->get_ratio() == SCREEN_RATIO_11);

    ui->menuFiltering->clear();
    QActionGroup * filtering_group = new QActionGroup(ui->menuFiltering);
    QAction * af1 = ui->menuFiltering->addAction(
        Emulator::tr("Nearest pixel"),
        [this]{e->set_filtering(SCREEN_FILTERING_NONE);}
        );
    af1->setActionGroup(filtering_group);
    af1->setCheckable(true);
    af1->setChecked(e->get_filtering() == SCREEN_FILTERING_NONE);

    QAction * af2 = ui->menuFiltering->addAction(
        Emulator::tr("Linear"),
        [this]{e->set_filtering(SCREEN_FILTERING_LINEAR);}
        );
    af2->setActionGroup(filtering_group);
    af2->setCheckable(true);
    af2->setChecked(e->get_filtering() == SCREEN_FILTERING_LINEAR);

    QAction * af3 = ui->menuFiltering->addAction(
        Emulator::tr("Anisotropic"),
        [this]{e->set_filtering(SCREEN_FILTERING_ANISOTROPIC);}
        );
    af3->setActionGroup(filtering_group);
    af3->setCheckable(true);
    af3->setChecked(e->get_filtering() == SCREEN_FILTERING_ANISOTROPIC);
}

void MainWindow::CreateDevicesMenu()
{
    ui->menuDevices->clear();

    for (unsigned int i=0; i < e->dm->device_count; i++)
    {
        QAction * a = ui->menuDevices->addAction(
                            e->dm->get_device(i)->device_name + " : " + e->dm->get_device(i)->device_type,
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

    int buttons_added=0;

    fdds_found = 0;

    QVector<ComputerDevice*>fdd_devices = e->dm->find_devices_by_class("fdd");
    fdds_found = fdd_devices.size();

    for (int i=0; i < fdds_found; i++) {
        FDD * fdd = dynamic_cast<FDD*>(fdd_devices[i]);
        fdds.push_back(fdd);
        CreateFDDMenu(i);
        if (fdd->get_loaded())
        {
            fdd_menu[i]->actions().at(0)->setText(fdd->file_name);
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

    QVector<ComputerDevice*>tape_devices = e->dm->find_devices_by_class("tape");
    if (tape_devices.size() != 0) {
        buttons_added++;
        QToolButton * tape_button = new QToolButton();
        tape_button->setIcon(QIcon(":/icons/tape"));
        tape_button->setFocusPolicy(Qt::NoFocus);

        connect(tape_button, &QToolButton::clicked, this, &MainWindow::on_actionTape_triggered);
        tape_action = ui->toolBar->insertWidget(ui->actionDebugger, tape_button);
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
        GenericDbgWnd * w = f(this, e, e->dm->get_device(i)->device);
        w->setAttribute(Qt::WA_DeleteOnClose);
        connect(w, &GenericDbgWnd::data_changed, DWM, &DebugWindowsManager::data_changed);
        connect(DWM, &DebugWindowsManager::update_all, w, &GenericDbgWnd::update_view);
        w->show();
    }
}

void MainWindow::keyPressEvent( QKeyEvent *event )
{
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key pressed: scan " << event->nativeScanCode() << "virtual" << event->nativeVirtualKey() << "key" << event->key() << "Qt::Key_Left" << (int)(Qt::Key_Left);
        //e->key_event(event, true);
        emit send_a_key(event, true);
    }
}

void MainWindow::keyReleaseEvent( QKeyEvent *event )
{
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key released:" << event->nativeScanCode() << event->nativeVirtualKey() << event->key();
        //e->key_event(event, false);
        emit send_a_key(event, false);
    }
}

void MainWindow::resizeEvent(QResizeEvent * event)
{
    //e->resize_screen();
}

void MainWindow::paintEvent(QPaintEvent * event)
{
    //QMainWindow::paintEvent(event);
    emit send_resize();
    //e->resize_screen();
}

void MainWindow::on_action_Cold_restart_triggered()
{
    emit send_reset(true);
    //e->reset(true);
}


void MainWindow::on_action_Soft_restart_triggered()
{
    emit send_reset(false);
    //e->reset(false);
}

void MainWindow::set_volume(int value)
{
    e->write_setup("Sound", "volume", QString::number(value));
    emit send_volume(value);
    //e->set_volume(value);
}

void MainWindow::set_mute(bool muted)
{
    e->write_setup("Sound", "muted", QString::number(muted?1:0));
    emit send_muted(muted);
    //e->set_muted(muted);
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
    setWindowTitle("eCat: " + sd->system_name + " : " + sd->system_version);
}

void MainWindow::load_config(QString file_name, bool set_default)
{
    if (e->loaded)
    {
        if (fdd_timer != nullptr) fdd_timer->stop();

        e->stop_video();
        e->quit();
        e->wait();
    }

    e->load_config(file_name);

    set_title();

    CreateDevicesMenu();
    UpdateToolbar();

    e->set_volume(volume->value());
    e->set_muted(mute->isChecked());

    #ifdef RENDERER_SDL2
        void* nativeView = reinterpret_cast<void*>(ui->screen->winId());
    #elif defined(RENDERER_QT)
        void* nativeView = reinterpret_cast<void*>(ui->screen);
    #endif

    if (nativeView) {
        e->init_video(nativeView);
        if (e->use_threads)
            e->start(QThread::TimeCriticalPriority);
        else
            e->run();

        if (set_default)
        {
            QString new_file = file_name.right(file_name.length() - e->work_path.length());
            e->write_setup("Startup", "default", new_file);
            qDebug() << new_file;
        }
    } else {
        qWarning() << "ui->screen->winId() is null!";
    }
}


void MainWindow::on_actionOpen_triggered()
{
    SystemData * sd = e->get_system_data();
    QString file_name = QFileDialog::getOpenFileName(this, Emulator::tr("Load a file"), last_path, sd->allowed_files);


    if (!file_name.isEmpty()) {
        QFileInfo fi(file_name);
        last_path = fi.absolutePath();
        e->write_setup("Startup", "last_path", last_path);

        HandleExternalFile(e, file_name);
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
            connect(w, &GenericDbgWnd::data_changed, DWM, &DebugWindowsManager::data_changed);
            connect(DWM, &DebugWindowsManager::update_all, w, &GenericDbgWnd::update_view);
            w->show();
    }
}

void MainWindow::closeEvent (QCloseEvent *event)
{
    fdds_found = 0; // to prevent crashing on buttons update
    e->stop_video();
    e->quit();
    e->wait();
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
        QString file_name = QFileDialog::getOpenFileName(this, MainWindow::tr("Open disk image"), last_path, fdds[n]->files);
        if (!file_name.isEmpty())
        {
            QFileInfo fi(file_name);
            fdd_menu[n]->actions().at(0)->setText(fi.fileName());
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_mount"));
            fdds[n]->load_image(file_name);
            last_path = fi.absolutePath();
            e->write_setup("Startup", "last_path", last_path);
        }
    }
}

void MainWindow::fdd_eject(unsigned int n)
{
    fdd_menu[n]->actions().at(0)->setText(MainWindow::tr("<Not loaded>"));
    fdd_button[n]->setIcon(QIcon(":/icons/floppy_unmount"));
    fdds[n]->unload();
}

void MainWindow::fdd_wp(unsigned int n)
{
    fdds[n]->change_protection();
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
        QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save disk image to a file"), last_path, fdds[n]->files_save, 0, QFileDialog::DontConfirmOverwrite);
        if (!file_name.isEmpty())
        {
            QMessageBox::StandardButton reply;

            if (fileExists(file_name))
            {
                reply = QMessageBox::question(this,
                                              MainWindow::tr("File already exists"),
                                              MainWindow::tr("File already exists. Overwrite? (Choose \"No\" to make a backup)"),
                                              QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
            } else {
                reply = QMessageBox::Yes;
            }

            if (reply == QMessageBox::Yes)
            {
                fdds[n]->save_image(file_name);
            } else
            if (reply == QMessageBox::No)
            {
                QString backup_name = file_name + ".bak";
                bool result = QFile::rename(file_name, backup_name);
                if (result)
                    fdds[n]->save_image(file_name);
                else
                    QMessageBox::critical(this, MainWindow::tr("Backup error"), MainWindow::tr("Error creating a backup. Probably *.bak already exists."));
            }
            QFileInfo fi(file_name);
            last_path = fi.absolutePath();
            e->write_setup("Startup", "last_path", last_path);
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
    SURFACE * s = e->get_surface();
    //SDL_SaveBMP(s, file_name.toUtf8().constData());

    unsigned int sx, sy;
    e->get_screen_constraints(&sx, &sy);

#ifdef RENDERER_SDL2
    unsigned int bitmap_size = sx*sy*4 + 200;

    std::vector<unsigned char> bmp;
    bmp.resize(bitmap_size);

    SDL_RWops *rw;
    rw = SDL_RWFromMem(bmp.data(), bitmap_size);
    SDL_SaveBMP_RW(s, rw, 0);
#endif

    QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save screenshot"), e->work_path, "PNG (*.png)");

    if (!file_name.isEmpty())
    {
        std::vector<unsigned char> image;
        unsigned error;
#ifdef RENDERER_SDL2
        error = decodeBMP(image, sx, sy, bmp);
#elif defined(RENDERER_QT)
        uint8_t * pixels = s->bits();
        image.insert(image.end(), pixels, pixels + sx*sy*4);
#endif
        std::vector<unsigned char> png;
        error = lodepng::encode(png, image, sx, sy);
        lodepng::save_file(png, file_name.toUtf8().constData());
    }
}


void MainWindow::on_actionAbout_triggered()
{
    QDialog * about = new QDialog(this);

    Ui_About aboutUi;
    aboutUi.setupUi(about);

    aboutUi.info_label->setText(
        aboutUi.info_label->text()
            .replace("{$PROJECT_VERSION}", PROJECT_VERSION)
            .replace("{$BUILD_ARCHITECTURE}", QSysInfo::buildCpuArchitecture())
            .replace("{$OS}", QSysInfo::productType())
            .replace("{$OS_VERSION}", QSysInfo::productVersion())
            .replace("{$CPU_ARCHITECTURE}", QSysInfo::currentCpuArchitecture())
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

