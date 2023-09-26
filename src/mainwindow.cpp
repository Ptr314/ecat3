#include <QDir>
#include <QFontDatabase>
#include <QEvent>

#include "dialogs/i8255window.h"
#include "mainwindow.h"
#include "qevent.h"
#include "ui_mainwindow.h"
#include "emulator/debug.h"
#include "dialogs/dumpwindow.h"
#include "dialogs/mmwindow.h"
#include "dialogs/debugwindow.h"
#include "dialogs/portwindow.h"

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

    QString current_path = QDir::currentPath(); //QApplication::applicationDirPath() ?

    e = new Emulator(current_path + "/computers/", current_path + "/data/", current_path + "/ecat.ini");

    QString file_to_load = e->read_setup("Startup", "default", "");

    e->load_config(file_to_load);


    DWM = new DebugWindowsManager();

    DWM->register_debug_window("rom", &CreateDumpWindow);
    DWM->register_debug_window("ram", &CreateDumpWindow);
    DWM->register_debug_window("memory_mapper", &CreateMMWindow);
    DWM->register_debug_window("i8080", &CreateDebugWindow);
    DWM->register_debug_window("port", &CreatePortWindow);
    DWM->register_debug_window("i8255", &CreateI8255Window);

    this->CreateDevicesMenu();

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
    //qDebug() << QString("%1:%2").arg(this->e->dm->get_device(i)->device_name).arg(this->e->dm->get_device(i)->device_type);
    //if (this->e->dm->get_device(i)->device_type == "rom")
    //{
    //    DumpWindow *w = new DumpWindow(this, this->e, this->e->dm->get_device(i)->device);
    //    w->setAttribute(Qt::WA_DeleteOnClose);
    //    w->show();
    //}
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

