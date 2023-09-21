#ifndef DEBUGWINDOW_H
#define DEBUGWINDOW_H

#include <QDialog>

#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/disasm.h"

namespace Ui {
class DebugWindow;
}

class DebugWindow : public QDialog
{
    Q_OBJECT

public:
    explicit DebugWindow(QWidget *parent = nullptr);
    explicit DebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
    ~DebugWindow();

private slots:

    void track();

    void on_closeButton_clicked();

    void on_stepButton_clicked();

    void on_toPCButton_clicked();

    void on_addBRButton_clicked();

    void on_removeBRButton_clicked();

    void on_runButton_clicked();

    void on_stopTrackingButton_clicked();

    void on_toolButton_clicked();

private:
    Ui::DebugWindow *ui;

    Emulator * e;
    CPU * cpu;
    DisAsm * disasm;
    bool stop_tracking;

    void update_registers();
};

QDialog * CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // DEBUGWINDOW_H
