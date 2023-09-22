#include "debugwindow.h"
#include "emulator/utils.h"
#include "ui_debugwindow.h"

DebugWindow::DebugWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DebugWindow),
    stop_tracking(false)
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


void DebugWindow::on_addBRButton_clicked()
{
    cpu->add_breakpoint(ui->codeview->get_address_at_cursor());
    ui->codeview->update();
}


void DebugWindow::on_removeBRButton_clicked()
{
    cpu->remove_breakpoint(ui->codeview->get_address_at_cursor());
    ui->codeview->update();

}


void DebugWindow::on_runButton_clicked()
{
    cpu->debug = DEBUG_OFF;
    stop_tracking = true;
}

void DebugWindow::track()
{
    if ((cpu->debug != DEBUG_STOPPED) && !stop_tracking)
    {
        ui->codeview->update();
        update_registers();
        QTimer::singleShot(200, this, SLOT(track()));
    }

    stop_tracking = false;
}

void DebugWindow::on_stopTrackingButton_clicked()
{
    cpu->debug = DEBUG_STOPPED;
    stop_tracking = true;
    on_toPCButton_clicked();
}


void DebugWindow::on_toolButton_clicked()
{
    unsigned int v = parse_numeric_value("$" + ui->valueEdit->text());
    cpu->set_context_value("PC", v);
    on_toPCButton_clicked();
}


void DebugWindow::on_stepOverButton_clicked()
{

}


void DebugWindow::on_runUntilButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        cpu->debug = DEBUG_BRAKES;
        QTimer::singleShot(200, this, SLOT(track()));
    }

}


void DebugWindow::on_gotoButton_clicked()
{
    unsigned int v = parse_numeric_value("$" + ui->valueEdit->text());
    ui->codeview->go_to(v);
    update_registers();
}

