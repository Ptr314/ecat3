#include <QDir>
#include <QFontDatabase>
#include <QEvent>
#include <QSlider>
#include <QFileDialog>

#include "dialogs/i8255window.h"
#include "mainwindow.h"
#include "qevent.h"
#include "ui_mainwindow.h"
#include "emulator/debug.h"
#include "dialogs/dumpwindow.h"
#include "dialogs/mmwindow.h"
#include "dialogs/debugwindow.h"
#include "dialogs/portwindow.h"
#include "dialogs/openconfigwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    QFontDatabase::addApplicationFont(":/fonts/mono-bold");
    QFontDatabase::addApplicationFont(":/fonts/mono-regular");
    QFontDatabase::addApplicationFont(":/fonts/mono-semibold");
    QFontDatabase::addApplicationFont(":/fonts/consolas");
    QFontDatabase::addApplicationFont(":/fonts/dos");

    ui->setupUi(this);

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

    e = new Emulator(work_path, current_path + "/data/", current_path + "/ecat.ini");

    QString file_to_load = e->read_setup("Startup", "default", "");

    e->load_config(work_path + file_to_load);

    DWM = new DebugWindowsManager();

    DWM->register_debug_window("rom", &CreateDumpWindow);
    DWM->register_debug_window("ram", &CreateDumpWindow);
    DWM->register_debug_window("memory_mapper", &CreateMMWindow);
    DWM->register_debug_window("i8080", &CreateDebugWindow);
    DWM->register_debug_window("port", &CreatePortWindow);
    DWM->register_debug_window("i8255", &CreateI8255Window);

    this->CreateDevicesMenu();

    QString sound_volume = e->read_setup("Startup", "volume", "50");
    volume->setValue(sound_volume.toInt());

    QString muted = e->read_setup("Startup", "muted", "0");
    mute->setChecked(muted.toInt() == 1);

    e->init_video((void*)(ui->screen->winId()));
    e->start();
}

MainWindow::~MainWindow()
{
    delete e;
    delete ui;
}

void MainWindow::CreateDevicesMenu()
{
    ui->menuDevices->clear();

    for (unsigned int i=0; i < this->e->dm->device_count; i++)
    {
        QAction * a = ui->menuDevices->addAction(
                            this->e->dm->get_device(i)->device_name + " : " + this->e->dm->get_device(i)->device_type,
                            [this, i=i]{onDeviceMenuCalled(i);}
                      );
        a->setEnabled( DWM->get_create_func(this->e->dm->get_device(i)->device_type) != nullptr );
    };


    //TODO: Interface adaptation to a machine configuration

}

void MainWindow::onDeviceMenuCalled(unsigned int i)
{
    DebugWndCreateFunc * f = DWM->get_create_func(this->e->dm->get_device(i)->device_type);
    if (f != nullptr)
    {
        QDialog * w = f(this, this->e, this->e->dm->get_device(i)->device);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
}

void MainWindow::on_actionExit_triggered()
{
    qApp->exit();
}

void MainWindow::keyPressEvent( QKeyEvent *event )
{
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key pressed: scan " << event->nativeScanCode() << "virtual" << event->nativeVirtualKey() << "key" << event->key() << "Qt::Key_Left" << (int)(Qt::Key_Left);
        e->key_event(event, true);
    }
}

void MainWindow::keyReleaseEvent( QKeyEvent *event )
{
    if (event->isAutoRepeat()) {
        event->ignore();
    } else {
        //qDebug() << "Key released:" << event->nativeScanCode() << event->nativeVirtualKey() << event->key();
        e->key_event(event, false);

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
}


void MainWindow::on_action_Soft_restart_triggered()
{
    e->reset(false);
}

void MainWindow::set_volume(int value)
{
    e->write_setup("Startup", "volume", QString::number(value));
    e->set_volume(value);
}

void MainWindow::set_mute(bool muted)
{
    e->write_setup("Startup", "muted", QString::number(muted?1:0));
    e->set_muted(muted);
    volume->setEnabled(!muted);
}

void MainWindow::on_action_Select_a_machine_triggered()
{
    QDialog * w = new OpenConfigWindow(this, e);
    w->setAttribute(Qt::WA_DeleteOnClose);
    connect(w, SIGNAL(load_config(QString, bool)), this, SLOT(load_config(QString, bool)));
    w->show();
}

void MainWindow::load_config(QString file_name, bool set_default)
{
    if (e->loaded)
    {
        e->stop_video();
        e->stop();

        e->load_config(file_name);

        e->set_volume(volume->value());
        e->set_muted(mute->isChecked());

        e->init_video((void*)(ui->screen->winId()));
        e->start();

        CreateDevicesMenu();
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
    QString file_name = QFileDialog::getOpenFileName(this, tr("Open XML File 1"), e->work_path, tr("All Files (*.*)"));
    RAM * m = dynamic_cast<RAM*>(e->dm->get_device_by_name("ram0"));
    uint8_t * buffer = m->get_buffer();

    QFile file(file_name);
    if (file.open(QIODevice::ReadOnly)){
        QByteArray data = file.readAll();
        memcpy(buffer, data.constData(), file.size());
        file.close();
    } else {
            QMessageBox::critical(0, MainWindow::tr("Error"), MainWindow::tr("Error opening file %1").arg(file_name));
    }
}

