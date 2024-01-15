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

#include "libs/lodepng/lodepng.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fdd_timer(nullptr)
    , fdds_found(0)
    , fdc(nullptr)
{
    QFontDatabase::addApplicationFont(":/fonts/mono-bold");
    QFontDatabase::addApplicationFont(":/fonts/mono-regular");
    QFontDatabase::addApplicationFont(":/fonts/mono-semibold");
    QFontDatabase::addApplicationFont(":/fonts/consolas");
    QFontDatabase::addApplicationFont(":/fonts/dos");

    ui->setupUi(this);

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

    QString current_path = QDir::currentPath(); //QApplication::applicationDirPath() ?

    QString work_path = current_path + "/computers/";
    QString software_path = current_path + "/software/";

    DWM = new DebugWindowsManager();

    DWM->register_debug_window("rom", &CreateDumpWindow);
    DWM->register_debug_window("ram", &CreateDumpWindow);
    DWM->register_debug_window("memory_mapper", &CreateMMWindow);
    DWM->register_debug_window("i8080", &CreateDebugWindow);
    DWM->register_debug_window("port", &CreatePortWindow);
    DWM->register_debug_window("port-address", &CreatePortWindow);
    DWM->register_debug_window("i8255", &CreateI8255Window);
    DWM->register_debug_window("z80", &CreateDebugWindow);
    DWM->register_debug_window("6502", &CreateDebugWindow);
    DWM->register_debug_window("65c02", &CreateDebugWindow);

    e = new Emulator(work_path, current_path + "/data/", software_path, current_path + "/ecat.ini");

    connect(this, &MainWindow::send_a_key,  e, &Emulator::key_event);
    connect(this, &MainWindow::send_volume, e, &Emulator::set_volume);
    connect(this, &MainWindow::send_muted,  e, &Emulator::set_muted);
    connect(this, &MainWindow::send_reset,  e, &Emulator::reset);
    connect(this, &MainWindow::send_resize, e, &Emulator::resize_screen);
    connect(this, &MainWindow::send_stop,   e, &Emulator::stop_emulation, Qt::QueuedConnection);

    for (unsigned int n=0; n < max_fdd_count; n++) CreateFDDMenu(n);
    ui->toolBar->insertSeparator(ui->actionDebugger);

    QString file_to_load = e->read_setup("Startup", "default", "");

    e->load_config(work_path + file_to_load);

    set_title();

    CreateDevicesMenu();



    QString sound_volume = e->read_setup("Sound", "volume", "50");
    volume->setValue(sound_volume.toInt());

    QString muted = e->read_setup("Sound", "muted", "0");
    mute->setChecked(muted.toInt() == 1);

    e->init_video((void*)(ui->screen->winId()));

    CreateScreenMenu();

    //qDebug() << "Main thread id: " << QCoreApplication::instance()->thread()->currentThreadId();
    if (e->use_threads)
        e->start(QThread::TimeCriticalPriority);
    else
        e->run();

}

void MainWindow::CreateFDDMenu(unsigned int n)
{
    fdd_menu[n] = new QMenu(this);
    QAction * a1 = new QAction(MainWindow::tr("<Not loaded>"));
    a1->setIcon(QIcon(":/icons/cdrom_unmount"));
    a1->setEnabled(false);
    QAction * a2 = new QAction(QString(MainWindow::tr("Open an image...")));
    a2->setIcon(QIcon(":/icons/open"));
    connect(a2, &QAction::triggered, this, [this, n=n](){fdd_open(n);});
    QAction * a3 = new QAction(QString(MainWindow::tr("Write protect")));
    connect(a3, &QAction::triggered, this, [this, n=n](){fdd_wp(n);});
    a3->setIcon(QIcon(":/icons/lock"));
    QAction * a4 = new QAction(QString(MainWindow::tr("Eject")));
    connect(a4, &QAction::triggered, this, [this, n=n](){fdd_eject(n);});
    a4->setIcon(QIcon(":/icons/eject"));
    QAction * a5 = new QAction(QString(MainWindow::tr("Write to a file...")));
    connect(a5, &QAction::triggered, this, [this, n=n](){fdd_write(n);});
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

    connect(fdd_button[n], &QToolButton::clicked, this, [this, n=n](){fdd_open(n);});


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
            [this, i=i]{e->set_scale(i);}
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
                            [this, i=i]{onDeviceMenuCalled(i);}
                      );
        a->setEnabled( DWM->get_create_func(e->dm->get_device(i)->device_type) != nullptr );
    };


    fdds_found = 0;

    //TODO: change "fdc" from setup
    fdc = dynamic_cast<FDC*>(e->dm->get_device_by_name("fdc", false));

    if (fdc != nullptr) {
        for (unsigned int i=0; i<max_fdd_count; i++)
        {
            FDD * fdd = dynamic_cast<FDD*>(e->dm->get_device_by_name(QString("fdd%1").arg(i), false));
            if (fdd != nullptr)
            {
                fdds[fdds_found] = fdd;
                if (fdd->get_loaded())
                {
                    fdd_menu[fdds_found]->actions().at(0)->setText(fdd->file_name);
                    fdd_button[fdds_found]->setIcon(QIcon(":/icons/floppy_mount"));
                    if (fdds[fdds_found]->is_protected())
                        fdd_button[fdds_found]->setIcon(QIcon(":/icons/floppy_locked"));
                }
                fdds_found++;
            }
        }

        if (fdds_found > 0)
        {
            for (unsigned int i = 0; i < max_fdd_count; i++)
            {
                if (fdds[i] != nullptr)
                    fdd_button[i]->setEnabled(true);
                else
                    fdd_button[i]->setEnabled(false);
            }

            if (fdd_timer == nullptr)
            {
                fdd_timer = new QTimer(this);
                connect(fdd_timer, &QTimer::timeout, this, &MainWindow::update_fdds);
            }
            fdd_timer->start(100);
        } else {
            if (fdd_timer != nullptr) fdd_timer->stop();

            for (unsigned int i = 0; i < max_fdd_count; i++)
            {
                fdd_button[i]->setIcon(QIcon(":/icons/floppy_unmount"));
                fdd_button[i]->setEnabled(false);
            }

        }
    }

    //TODO: Interface adaptation to a machine configuration

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

        e->load_config(file_name);

        set_title();

        CreateDevicesMenu();

        e->set_volume(volume->value());
        e->set_muted(mute->isChecked());

        e->init_video((void*)(ui->screen->winId()));
        if (e->use_threads)
            e->start(QThread::TimeCriticalPriority);
        else
            e->run();
    }

    if (set_default)
    {
        QString new_file = file_name.right(file_name.length() - e->work_path.length());
        e->write_setup("Startup", "default", new_file);
        qDebug() << new_file;
    }
}


void MainWindow::on_actionOpen_triggered()
{
    QString path = e->read_setup("Startup", "last_path", e->work_path);
    SystemData * sd = e->get_system_data();
    QString file_name = QFileDialog::getOpenFileName(this, Emulator::tr("Load a file"), path, sd->allowed_files);


    if (!file_name.isEmpty()) {
        QFileInfo fi(file_name);
        e->write_setup("Startup", "last_path", fi.absolutePath());

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
        QString file_name = QFileDialog::getOpenFileName(this, MainWindow::tr("Open disk image"), e->work_path, fdds[n]->files);
        if (!file_name.isEmpty())
        {
            QFileInfo fi(file_name);
            fdd_menu[n]->actions().at(0)->setText(fi.fileName());
            fdd_button[n]->setIcon(QIcon(":/icons/floppy_mount"));
            fdds[n]->load_image(file_name);
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
        QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save disk image to a file"), e->work_path, fdds[n]->files, 0, QFileDialog::DontConfirmOverwrite);
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
                //QFileInfo fi(file_name);
                QString backup_name = file_name + ".bak";
                bool result = QFile::rename(file_name, backup_name);
                if (result)
                    fdds[n]->save_image(file_name);
                else
                    QMessageBox::critical(this, MainWindow::tr("Backup error"), MainWindow::tr("Error creating a backup. Probably *.bak already exists."));
            }
        }
    }

}

void MainWindow::update_fdds()
{
    //TODO: this event may happen after exiting or stopping emulator
    for (unsigned int i=0; i<fdds_found; i++)
    {
        if (fdc->get_busy() && fdc->get_selected_drive()==i) {
            fdd_button[i]->setIcon(QIcon(":/icons/floppy_access"));
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
    SDL_Surface * s = e->get_surface();
    //SDL_SaveBMP(s, file_name.toUtf8().constData());

    unsigned int sx, sy;
    e->get_screen_constraints(&sx, &sy);

    unsigned int bitmap_size = sx*sy*4 + 200;

    std::vector<unsigned char> bmp;
    bmp.resize(bitmap_size);

    SDL_RWops *rw;
    rw = SDL_RWFromMem(bmp.data(), bitmap_size);
    SDL_SaveBMP_RW(s, rw, 0);

    QString file_name = QFileDialog::getSaveFileName(this, MainWindow::tr("Save screenshot"), e->work_path, "PNG (*.png)");

    if (!file_name.isEmpty())
    {
        std::vector<unsigned char> image;
        unsigned error = decodeBMP(image, sx, sy, bmp);
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
        aboutUi.info_label->text().replace("{$PROJECT_VERSION}", PROJECT_VERSION)
    );


    about->exec();
}

