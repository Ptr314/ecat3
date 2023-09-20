#include "dumpwindow.h"
#include "emulator/utils.h"
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
    this->setWindowTitle(d->name + " : " + d->type);
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

void DumpWindow::on_toolButton_clicked()
{
    QString s = ui->addressEdit->text();
    QStringList parts = s.split(':');
    unsigned int a = parse_numeric_value("$" + parts.at(1));
    ui->dump_area->go_to(a);
}

