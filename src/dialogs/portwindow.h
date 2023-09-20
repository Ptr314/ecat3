#ifndef PORTWINDOW_H
#define PORTWINDOW_H

#include "emulator/core.h"
#include "emulator/emulator.h"
#include <QDialog>

namespace Ui {
class PortWindow;
}

class PortWindow : public QDialog
{
    Q_OBJECT

public:
    explicit PortWindow(QWidget *parent = nullptr);
    PortWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
    ~PortWindow();

private:
    Ui::PortWindow *ui;

    Emulator * e;
    Port * d;
    QTimer * timer;

private slots:
    void update();
};

QDialog * CreatePortWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // PORTWINDOW_H
