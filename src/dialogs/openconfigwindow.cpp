#include "openconfigwindow.h"
#include "ui_openconfigwindow.h"

OpenConfigWindow::OpenConfigWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OpenConfigWindow)
{
    ui->setupUi(this);
}

OpenConfigWindow::~OpenConfigWindow()
{
    delete ui;
}
