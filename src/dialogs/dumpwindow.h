#ifndef DUMPWINDOW_H
#define DUMPWINDOW_H

#include <QDialog>

#include "emulator/emulator.h"

namespace Ui {
class DumpWindow;
}

class DumpWindow : public QDialog
{
    Q_OBJECT

public:
    explicit DumpWindow(QWidget *parent = nullptr);
    explicit DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * device);
    ~DumpWindow();

private slots:
    void on_closeButton_clicked();

    void on_toolButton_clicked();

private:
    Ui::DumpWindow *ui;

    Emulator * e;
    ComputerDevice * d;
};

QDialog * CreateDumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // DUMPWINDOW_H
