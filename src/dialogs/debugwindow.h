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
    void on_closeButton_clicked();

private:
    Ui::DebugWindow *ui;

    Emulator * e;
    ComputerDevice * d;
    DisAsm * disasm;
};

QDialog * CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // DEBUGWINDOW_H
