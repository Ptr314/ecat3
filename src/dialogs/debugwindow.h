#ifndef DEBUGWINDOW_H
#define DEBUGWINDOW_H

#include <QDialog>

#include "dialogs/genericdbgwnd.h"
#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/disasm.h"

namespace Ui {
class DebugWindow;
}

class DebugWindow : public GenericDbgWnd
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

    void on_stepOverButton_clicked();

    void on_runUntilButton_clicked();

    void on_gotoButton_clicked();

    void on_runDebuggedButton_clicked();

    void update_state();

    void on_dumpGotoButton_clicked();

    void on_dumpPgDownBotton_clicked();

    void on_dumpPgUpBotton_clicked();

public slots:
    void command_key(QKeyEvent *event);
    virtual void update_view() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Ui::DebugWindow *ui;

    Emulator * e;
    CPU * cpu;
    DisAsm * disasm;
    bool stop_tracking;
    int temporary_break;

    QTimer * state_timer;

    AddressableDevice * device_mm;
    AddressableDevice * device_memory[100];
    int memory_devices;

    void update_registers();

    void resizeEvent(QResizeEvent*) override;
};

GenericDbgWnd *CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // DEBUGWINDOW_H
