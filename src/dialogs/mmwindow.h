#ifndef MMWINDOW_H
#define MMWINDOW_H

#include <QDialog>

#include "emulator/emulator.h"

namespace Ui {
class MemoryMapperWindow;
}

class MemoryMapperWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MemoryMapperWindow(QWidget *parent = nullptr);
    explicit MemoryMapperWindow(QWidget *parent, Emulator * e, ComputerDevice * device);
    ~MemoryMapperWindow();

private slots:
    void on_pushButton_clicked();

    void on_process_button_clicked();

private:
    Ui::MemoryMapperWindow *ui;

    Emulator * e;
    ComputerDevice * d;
};

QDialog * CreateMMWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // MMWINDOW_H
