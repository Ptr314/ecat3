#ifndef OPENCONFIGWINDOW_H
#define OPENCONFIGWINDOW_H

#include <QDialog>
#include <QStandardItem>

//struct MachineFile {
//    QString type, name, version, path;
//};

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
    OpenConfigWindow(QWidget *parent, QString work_path);
    ~OpenConfigWindow();

private:
    Ui::OpenConfigWindow *ui;

    //MachineFile machines[100];
    //unsigned int machine_count;
    //ComputerFamily * families[100];
    //unsigned int families_count;
    //ComputerModel * models[200];
    //unsigned int models_count;
    //QStandardItemModel * model;

    void list_machines(QString work_path);

public slots:
    void set_description(const QModelIndex & index);
};

#endif // OPENCONFIGWINDOW_H
