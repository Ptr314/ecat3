#ifndef DUMPWINDOW_H
#define DUMPWINDOW_H

#include <QDialog>

#include "emulator/emulator.h"
#include "emulator/core.h"
#include "dialogs/debugdialog.h"

namespace Ui {
class DumpWindow;
}

class DumpWindow : public DebugDialog
{
    Q_OBJECT

public:
    explicit DumpWindow(QWidget *parent = nullptr);
    explicit DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * device);
    ~DumpWindow();

    static DebugDialog CreateDialog(QWidget *parent, Emulator * e, ComputerDevice * device);

private slots:
    void on_closeButton_clicked();

private:
    Ui::DumpWindow *ui;
};

#endif // DUMPWINDOW_H
