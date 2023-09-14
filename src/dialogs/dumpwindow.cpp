#include "dumpwindow.h"
#include "ui_dumpwindow.h"

DumpWindow::DumpWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DumpWindow)
{
    ui->setupUi(this);
}

DumpWindow::DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    DumpWindow(parent)
{
    this->e = e;
    this->d = d;
    ui->dump_area->set_data(e, (AddressableDevice*)d);
}

DumpWindow::~DumpWindow()
{
    delete ui;
}

void DumpWindow::on_closeButton_clicked()
{
    this->close();
}

QDialog * CreateDumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new DumpWindow(parent, e, d);
}
