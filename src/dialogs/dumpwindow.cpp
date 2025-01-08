#include <QKeyEvent>
#include <QFileDialog>

#include "dumpwindow.h"
#include "emulator/utils.h"
#include "qexception.h"
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
    QString so = ui->offsetEdit->text();
    QString sa = ui->addressEdit->text();
    try {
        unsigned int offset = parse_numeric_value("$" + so);
        unsigned int address = parse_numeric_value("$" + sa);
        ui->dump_area->go_to(offset, address);
    } catch (QException &e) {
        QMessageBox::critical(0, DumpWindow::tr("Error"), DumpWindow::tr("Incorrect address value"));
    }
}

void DumpWindow::keyPressEvent(QKeyEvent *event)
{
    if(event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
        return;
    else
    if(event->key() == Qt::Key_PageDown)
    {
        on_pgDnButton_clicked();
        return;
    }
    else
    if(event->key() == Qt::Key_PageUp)
    {
        on_pgUpButton_clicked();
        return;
    }
    QDialog::keyPressEvent(event);
}

void DumpWindow::update_view()
{
    ui->dump_area->update_view();
}

void DumpWindow::on_pgDnButton_clicked()
{
    ui->dump_area->page_down();
}


void DumpWindow::on_pgUpButton_clicked()
{
    ui->dump_area->page_up();
}


void DumpWindow::on_topButton_clicked()
{
    ui->dump_area->go_to(0);
}


void DumpWindow::on_bottomButton_clicked()
{
    ui->dump_area->go_to(-1);
}


void DumpWindow::on_saveButton_clicked()
{
    QString path = e->read_setup("Startup", "last_path", e->work_path);
    QString file_name = QFileDialog::getSaveFileName(this, DumpWindow::tr("Save contents to a file"), path, DumpWindow::tr("Dump (*.bin)"));
    if (!file_name.isEmpty())
    {
        Memory * dev = dynamic_cast<Memory*>(d);
        QFile file(file_name);
        if (file.open(QIODevice::WriteOnly)){
            file.write(reinterpret_cast<char*>(dev->get_buffer()), dev->get_size());
            file.close();
        }
        // QFileInfo fi(file_name);
        // e->write_setup("Startup", "last_path", fi.absolutePath());
    }
}

