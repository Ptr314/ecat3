#ifndef TAPERECORDERWND_H
#define TAPERECORDERWND_H

#include <QDialog>

#include "dialogs/genericdbgwnd.h"
#include "emulator/core.h"
#include "emulator/emulator.h"
#include "emulator/devices/common/tape.h"

namespace Ui {
class TapeRecorderWindow;
}

class TapeRecorderWindow : public GenericDbgWnd
{
    Q_OBJECT

public:
    explicit TapeRecorderWindow(QWidget *parent = nullptr);
    TapeRecorderWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

    ~TapeRecorderWindow();

private:
    Ui::TapeRecorderWindow *ui;

    Emulator * e;
    TapeRecorder * d;
private slots:
    void set_mute(bool muted);
    void on_openButton_clicked();
};

#endif // TAPERECORDERWND_H
