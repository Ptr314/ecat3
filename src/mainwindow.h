#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "emulator/emulator.h"
#include "emulator/debug.h"

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

protected:
    void keyPressEvent( QKeyEvent *event );
    void keyReleaseEvent( QKeyEvent *event );

private slots:
    void on_actionExit_triggered();
    void onDeviceMenuCalled(unsigned int i);

    void on_action_Cold_restart_triggered();

    void on_action_Soft_restart_triggered();

private:
    Ui::MainWindow *ui;

    DebugWindowsManager * DWM;

    void CreateDevicesMenu();
};
#endif // MAINWINDOW_H
