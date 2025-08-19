// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Port debug window, source

#include "portwindow.h"
#include "ui_portwindow.h"

#include "emulator/devices/common/register.h"

PortWindow::PortWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::PortWindow)
{
    ui->setupUi(this);
}

PortWindow::PortWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    PortWindow(parent)
{
    this->e = e;
    this->d = d;
    setWindowTitle(d->name + " : " + d->type);

    timer = new QTimer();
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(100);
}

PortWindow::~PortWindow()
{
    delete ui;
}

void PortWindow::update()
{
    unsigned v;
    if (auto dev = dynamic_cast<Port*>(d)) {
        v = dev->get_direct();
    } else
    if (auto dev = dynamic_cast<Register*>(d)) {
        v = dev->get_value() & 0xFF;
    } else {
        qDebug() << "PortWindow: unknown class";
        return;
    }
    ui->binaryEdit->setText(QString("%1").arg(v, 8, 2, QChar('0')));
    ui->hexEdit->setText(QString("%1").arg(v, 2, 16, QChar('0')).toUpper());
}

GenericDbgWnd * CreatePortWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new PortWindow(parent, e, d);
}
