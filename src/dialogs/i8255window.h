#ifndef I8255WINDOW_H
#define I8255WINDOW_H

#include "dialogs/genericdbgwnd.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/emulator.h"
#include <QDialog>

namespace Ui {
class I8255Window;
}

class I8255Window : public GenericDbgWnd
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

GenericDbgWnd *CreateI8255Window(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // I8255WINDOW_H
