#ifndef I8255WINDOW_H
#define I8255WINDOW_H

#include "emulator/devices/common/i8255.h"
#include "emulator/emulator.h"
#include <QDialog>

namespace Ui {
class I8255Window;
}

class I8255Window : public QDialog
{
    Q_OBJECT

public:
    explicit I8255Window(QWidget *parent = nullptr);
    I8255Window(QWidget *parent, Emulator * e, ComputerDevice * d);
    ~I8255Window();

private:
    Ui::I8255Window *ui;

    Emulator * e;
    I8255 * d;
    QTimer * timer;

private slots:
    void update();
};

QDialog * CreateI8255Window(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // I8255WINDOW_H
