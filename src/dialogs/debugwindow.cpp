#include "debugwindow.h"
#include "ui_debugwindow.h"

DebugWindow::DebugWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DebugWindow)
{
    ui->setupUi(this);
}

DebugWindow::DebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    DebugWindow(parent)
{
    this->e = e;
    this->cpu = dynamic_cast<CPU*>(d);

    this->setWindowTitle(d->name + " : " + d->type);

    QString file_name = e->data_path + "i8080.dis";
    disasm = new DisAsm(this, file_name);
    ui->codeview->set_data(e, dynamic_cast<CPU*>(d), disasm, dynamic_cast<CPU*>(d)->get_pc());
    update_registers();
}

DebugWindow::~DebugWindow()
{
    delete ui;
}

void DebugWindow::update_registers()
{
    QList<QString> r = cpu->get_registers();
    QList<QString> f = cpu->get_flags();
    ui->registers->set_data(r);
    ui->flags->set_data(f);

}

QDialog * CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new DebugWindow(parent, e, d);
}

void DebugWindow::on_closeButton_clicked()
{
    this->close();
}


void DebugWindow::on_stepButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        cpu->debug = DEBUG_STEP;
        QTimer::singleShot(100, this, SLOT(on_toPCButton_clicked()));
    }
}


void DebugWindow::on_toPCButton_clicked()
{
    ui->codeview->go_to(cpu->get_pc());
    update_registers();
}

