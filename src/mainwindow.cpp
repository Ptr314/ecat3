#include <QDir>
#include <QFontDatabase>

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    int font_id = QFontDatabase::addApplicationFont(":/fonts/mono");
    qDebug() << "Font " << font_id;
    qDebug() << QFontDatabase::applicationFontFamilies(font_id);

    ui->setupUi(this);

    QString current_path = QDir::currentPath(); //QApplication::applicationDirPath() ?

    e = new Emulator(current_path + "/computers/", current_path + "/ecat.ini");

    QString file_to_load = e->read_setup("Startup", "default", "");
    qDebug() << "File to load: " + file_to_load;

    e->load_config(file_to_load);
    //E.Start(UseDI, Handle);
    this->CreateDevicesMenu();

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
        //QAction * a = new QAction();
        ui->menuDevices->addAction(
            this->e->dm->get_device(i)->device_name + " : " + this->e->dm->get_device(i)->device_type,
            [this, i=i]{onDeviceMenuCalled(i);}
        );
    };

    //TODO: Interface adaptation to a machine configuration

}

void MainWindow::onDeviceMenuCalled(unsigned int i)
{
    qDebug() << QString("%1:%2").arg(this->e->dm->get_device(i)->device_name).arg(this->e->dm->get_device(i)->device_type);
    if (this->e->dm->get_device(i)->device_type == "rom")
    {
        DumpWindow *w = new DumpWindow(this, this->e, this->e->dm->get_device(i)->device);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
}

void MainWindow::on_actionExit_triggered()
{
    qApp->exit();
}

