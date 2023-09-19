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
    this->d = d;
    QString file_name = e->data_path + "i8080.dis";
    disasm = new DisAsm(this, file_name);
    ui->codeview->set_data(e, dynamic_cast<CPU*>(d), disasm, 0xF800); //dynamic_cast<CPU*>(d)->get_pc()
}

DebugWindow::~DebugWindow()
{
    delete ui;
}

QDialog * CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new DebugWindow(parent, e, d);
}

void DebugWindow::on_closeButton_clicked()
{
    this->close();
}

