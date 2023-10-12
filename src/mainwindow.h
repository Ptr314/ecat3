#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSlider>
#include <QToolButton>

#include "emulator/emulator.h"
#include "emulator/debug.h"
#include "emulator/devices/common/fdd.h"

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
    void closeEvent(QCloseEvent *event);

private slots:
    void load_config(QString file_name, bool set_default);

    void onDeviceMenuCalled(unsigned int i);

    void on_action_Cold_restart_triggered();

    void on_action_Soft_restart_triggered();

    void set_volume(int value);
    void set_mute(bool muted);

    void on_action_Select_a_machine_triggered();

    void on_actionOpen_triggered();

    void on_actionDebugger_triggered();

    //void show_screen();

    void on_action_Exit_triggered();

    void fdd_open(unsigned int n);
    void fdd_eject(unsigned int n);
    void fdd_wp(unsigned int n);
    void fdd_write(unsigned int n);

    void update_fdds();

    void on_actionScreenshot_triggered();

signals:
    void send_a_key(QKeyEvent *event, bool press);
    void send_volume(int value);
    void send_muted(bool muted);
    void send_reset(bool cool);
    void send_resize();
    void send_stop();

private:
    Ui::MainWindow *ui;

    DebugWindowsManager * DWM;

    QSlider * volume;
    QToolButton * mute;

    QToolButton * fdd_button[2];
    QMenu * fdd_menu[2];
    FDD * fdds[2];
    FDC * fdc;
    unsigned int max_fdd_count = 2;
    unsigned int fdds_found = 0;
    QTimer * fdd_timer;


    void CreateDevicesMenu();
    void CreateFDDMenu(unsigned int n);
};
#endif // MAINWINDOW_H
