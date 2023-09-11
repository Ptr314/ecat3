#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "emulator/emulator.h"
#include "dialogs/dumpwindow.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    Emulator *e;

private slots:
    void on_actionExit_triggered();
    void onDeviceMenuCalled(unsigned int i);

private:
    Ui::MainWindow *ui;

    void CreateDevicesMenu();
};
#endif // MAINWINDOW_H
