#include "dumpwindow.h"
#include "ui_dumpwindow.h"

#include "emulator/debug.h"

DumpWindow::DumpWindow(QWidget *parent) :
    DebugDialog(parent),
    ui(new Ui::DumpWindow)
{
    ui->setupUi(this);
}

DumpWindow::DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * device):
    DebugDialog(parent, e, device)
{
    //ui->dump_area->set_memory((Memory*)device);
}

DumpWindow::~DumpWindow()
{
    delete ui;
}

DebugDialog DumpWindow::CreateDialog(QWidget *parent, Emulator * e, ComputerDevice * device)
{
    return new DumpWindow(parent, e, device);
}


void DumpWindow::on_closeButton_clicked()
{
    this->close();
}

