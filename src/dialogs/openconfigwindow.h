#ifndef OPENCONFIGWINDOW_H
#define OPENCONFIGWINDOW_H

#include <QDialog>
#include <QStandardItem>

#include "emulator/emulator.h"

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

    ComputerModel(QString type, QString name, QString version, QString path);
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
    void on_closeButton_clicked();

    void on_okButton_clicked();

signals:
    void load_config(QString file_name, bool set_default);
};

#endif // OPENCONFIGWINDOW_H
