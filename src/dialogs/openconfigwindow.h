// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Computer selection window, header

#pragma once

#include <QDialog>
#include <QStandardItem>

#include "emulator/emulator.h"

// Items of the machine tree. Families stay alphabetical; the models inside a
// family are ordered by "order" from the system section of the config first
// and only then alphabetically, so a config can take a place of its own in
// its family while everything without an "order" keeps the order it had.
class ComputerFamily: public QStandardItem
{
public:
    QString type;

    ComputerFamily(QString type, QString name);

};

class ComputerModel: public QStandardItem
{
public:
    QString type;
    QString name;
    QString path;
    int order;

    ComputerModel(QString type, QString name, QString version, QString path, int order);

    bool operator<(const QStandardItem &other) const override;
};


namespace Ui {
class OpenConfigWindow;
}

class OpenConfigWindow : public QDialog
{
    Q_OBJECT

public:
    explicit OpenConfigWindow(QWidget *parent = nullptr);
    OpenConfigWindow(QWidget *parent, Emulator * e);
    ~OpenConfigWindow();

private:
    Ui::OpenConfigWindow *ui;

    Emulator * e;

    QString selected_path;

    void list_machines(QString work_path);

public slots:
    void set_description(QModelIndex index);
private slots:
    void on_item_double_clicked(QModelIndex index);
    void on_closeButton_clicked();

    void on_debugCheck_toggled(bool checked);

    void on_okButton_clicked();

signals:
    void load_config(QString file_name, bool set_default);
};
