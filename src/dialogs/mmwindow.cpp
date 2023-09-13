#include "mmwindow.h"
#include "ui_mmwindow.h"

MemoryMapperWindow::MemoryMapperWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MemoryMapperWindow)
{
    ui->setupUi(this);
}

MemoryMapperWindow::~MemoryMapperWindow()
{
    delete ui;
}
