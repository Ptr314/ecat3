#include <QKeyEvent>

#include "dumpwindow.h"
#include "emulator/utils.h"
#include "ui_dumpwindow.h"

DumpWindow::DumpWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::DumpWindow)
{
    ui->setupUi(this);
}

DumpWindow::DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    DumpWindow(parent)
{
    this->e = e;
    this->d = d;
    ui->dump_area->set_data(e, dynamic_cast<AddressableDevice*>(d));
    ui->dump_area->set_frame(true, true, true, true);
    setWindowTitle(d->name + " : " + d->type);
}

DumpWindow::~DumpWindow()
{
    delete ui;
}

void DumpWindow::on_closeButton_clicked()
{
    close();
}

GenericDbgWnd * CreateDumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
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

void DumpWindow::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
        return;
    QDialog::keyPressEvent(event);
}

void DumpWindow::update_view()
{
    ui->dump_area->update_view();
}
