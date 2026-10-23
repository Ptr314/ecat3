// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory manager window, source

#include "mmwindow.h"
#include "ui_mmwindow.h"

#include <QLayout>
#include <QMessageBox>

#include "emulator/utils.h"
#include "emulator/emulator.h"
#include "dialogs/numfmt.h"
#include "dialogs/debugger/memoryview.h"

MemoryMapperWindow::MemoryMapperWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::MemoryMapperWindow),
    panel(nullptr)
{
    ui->setupUi(this);
}

MemoryMapperWindow::MemoryMapperWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    MemoryMapperWindow(parent)
{
    this->e = e;
    this->d = d;
    base = view_base(d);
    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    ui->address_edit->setText(fmt_num(0, base, addr_digits(0, base)));
}

void MemoryMapperWindow::update_view()
{
    if (panel != nullptr) panel->refresh();
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
    unsigned int cfg, address;
    try {
        cfg = parse_input(ui->config_edit->text(), base);
        address = parse_input(ui->address_edit->text(), base);
    } catch (const std::exception &) {
        QMessageBox::critical(this, MemoryMapperWindow::tr("Error"), MemoryMapperWindow::tr("Incorrect address value"));
        return;
    }
    // qDebug() << cfg << address;
    unsigned int address_on_device, range_index;

    AddressableDevice * d = dynamic_cast<MemoryMapper*>(this->d)->map_memory(
                                                                  cfg,
                                                                  address,
                                                                  ui->readButton->isChecked()?MODE_R:MODE_W,
                                                                  &address_on_device,
                                                                  &range_index
                                                                );

    //Nothing answers there. And no get_value(): the window looks, it does not
    //read - a read strobes a port or clears a register
    if (d == nullptr) {
        ui->device_name->setText(MemoryMapperWindow::tr("No device"));
        return;
    }
    QString s = QString::fromStdString(d->name + " " + d->type);
    ui->device_name->setText(s);

    delete panel;
    panel = new MemoryPanel(e, memory_of_device(d), ui->frame_2);
    ui->frame_2->layout()->addWidget(panel);
    panel->view()->go_to(address_on_device);
}

GenericDbgWnd * CreateMMWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new MemoryMapperWindow(parent, e, d);
}
