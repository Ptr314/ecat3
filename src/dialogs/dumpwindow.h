#ifndef DUMPWINDOW_H
#define DUMPWINDOW_H

#include <QDialog>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

namespace Ui {
class DumpWindow;
}

class DumpWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit DumpWindow(QWidget *parent = nullptr);
    explicit DumpWindow(QWidget *parent, Emulator * e, ComputerDevice * device);
    ~DumpWindow();

private slots:
    void on_closeButton_clicked();

    void on_toolButton_clicked();

    void on_pgDnButton_clicked();

    void on_pgUpButton_clicked();

    void on_topButton_clicked();

    void on_bottomButton_clicked();

    void on_saveButton_clicked();

public slots:
    virtual void update_view() override;

private:
    Ui::DumpWindow *ui;

    Emulator * e;
    ComputerDevice * d;

    void keyPressEvent(QKeyEvent *event) override;
};

GenericDbgWnd *CreateDumpWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

#endif // DUMPWINDOW_H
