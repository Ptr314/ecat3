#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSlider>
#include <QToolButton>

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
    void keyPressEvent( QKeyEvent * event );
    void keyReleaseEvent( QKeyEvent * event );
    void resizeEvent(QResizeEvent * event);
    void paintEvent(QPaintEvent * event);

private slots:
    void load_config(QString file_name, bool set_default);

    void on_actionExit_triggered();
    void onDeviceMenuCalled(unsigned int i);

    void on_action_Cold_restart_triggered();

    void on_action_Soft_restart_triggered();

    void set_volume(int value);
    void set_mute(bool muted);

    void on_action_Select_a_machine_triggered();

    void on_actionOpen_triggered();

private:
    Ui::MainWindow *ui;

    DebugWindowsManager * DWM;

    QSlider * volume;
    QToolButton * mute;

    void CreateDevicesMenu();
};
#endif // MAINWINDOW_H
