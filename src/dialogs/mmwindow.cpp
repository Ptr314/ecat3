// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory manager window, source

#include "mmwindow.h"
#include "ui_mmwindow.h"

#include "emulator/utils.h"
#include "emulator/emulator.h"

MemoryMapperWindow::MemoryMapperWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::MemoryMapperWindow)
{
    ui->setupUi(this);
}

MemoryMapperWindow::MemoryMapperWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    MemoryMapperWindow(parent)
{
    this->e = e;
    this->d = d;
    setWindowTitle(d->name + " : " + d->type);
}

MemoryMapperWindow::~MemoryMapperWindow()
{
    delete ui;
}

void MemoryMapperWindow::on_pushButton_clicked()
{
    close();
}

void MemoryMapperWindow::on_process_button_clicked()
{
    unsigned int cfg = parse_numeric_value("$" + ui->config_edit->text());
    unsigned int address = parse_numeric_value("$" + ui->address_edit->text());
    qDebug() << cfg << address;
    unsigned int address_on_device, range_index;

    AddressableDevice * d = dynamic_cast<MemoryMapper*>(this->d)->map_memory(
                                                                  cfg,
                                                                  address,
                                                                  ui->readButton->isChecked()?MODE_R:MODE_W,
                                                                  &address_on_device,
                                                                  &range_index
                                                                );

    unsigned int value = d->get_value(address_on_device);
    QString s = d->name + " " + d->type;
    ui->device_name->setText(s);

    ui->dump_area->set_data(e, dynamic_cast<AddressableDevice*>(d), address_on_device);
    ui->dump_area->update();
}

GenericDbgWnd * CreateMMWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new MemoryMapperWindow(parent, e, d);
}
