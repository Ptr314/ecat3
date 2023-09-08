#include <QDir>

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QString current_path = QDir::currentPath(); //QApplication::applicationDirPath() ?

    e = new Emulator(current_path + "/computers/", current_path + "/ecat.ini");

    QString file_to_load = e->read_setup("Startup", "default", "");
    qDebug() << "File to load: " + file_to_load;

    e->load_config(file_to_load);
    //E.Start(UseDI, Handle);
    //CreateDevicesMenu

}

MainWindow::~MainWindow()
{
    delete e;
    delete ui;
}


void MainWindow::on_actionExit_triggered()
{
    qApp->exit();
}

