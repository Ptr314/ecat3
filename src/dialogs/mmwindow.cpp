// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory manager window, source

#include "mmwindow.h"
#include "ui_mmwindow.h"

#include <QMessageBox>

#include "emulator/utils.h"
#include "emulator/emulator.h"
#include "dialogs/numfmt.h"

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
    has_device = false;
    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    ui->dump_area->set_frame(true, true, true, true);
    // As the dump window: an octal machine starts in octal words, any other
    // in hex bytes
    const bool octal = view_base(d) == 8;
    ui->address_edit->setText(fmt_num(0, octal ? 8 : 16, addr_digits(0, octal ? 8 : 16)));
    ui->baseButton->setChecked(octal);
    ui->wordButton->setChecked(octal);
    apply_view();
}

void MemoryMapperWindow::apply_view()
{
    const int base = ui->baseButton->isChecked() ? 8 : 16;
    const bool word = ui->wordButton->isChecked();
    ui->baseButton->setText(base == 8 ? "OCT" : "HEX");
    ui->wordButton->setText(word ? "16" : "8");
    ui->dump_area->set_view(base, word);
}

void MemoryMapperWindow::on_baseButton_toggled(bool)
{
    apply_view();
}

void MemoryMapperWindow::on_wordButton_toggled(bool)
{
    apply_view();
}

void MemoryMapperWindow::update_view()
{
    if (has_device) ui->dump_area->update_view();
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
        const int base = ui->dump_area->get_base();
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

    ui->dump_area->set_data(e, dynamic_cast<AddressableDevice*>(d), address_on_device);
    has_device = true;
    ui->dump_area->update();
}

GenericDbgWnd * CreateMMWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new MemoryMapperWindow(parent, e, d);
}
