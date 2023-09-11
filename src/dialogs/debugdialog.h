#ifndef DEBUGDIALOG_H
#define DEBUGDIALOG_H

#include <QDialog>

#include "emulator/emulator.h"
#include "emulator/core.h"

class DebugDialog : public QDialog
{
    Q_OBJECT
private:
    Emulator * e;
    ComputerDevice * d;

public:
    DebugDialog(QWidget *parent);
    DebugDialog(QWidget *parent, Emulator * e, ComputerDevice * device);

    static DebugDialog CreateDialog(QWidget *parent, Emulator * e, ComputerDevice * device);
};

#endif // DEBUGDIALOG_H
