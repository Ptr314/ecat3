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

protected:
    QIcon btnIconOff;
    QIcon btnIconOn;
    QIcon btnIconEjectOff;
    QIcon btnIconEjectOn;

    bool is_playing;
    bool is_paused;

    void play_pause();

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Ui::TapeRecorderWindow *ui;

    Emulator * e;
    TapeRecorder * d;

    QPoint dragPosition;

private slots:
    void set_mute(bool muted);
    void on_buttonEject_released();
    void on_buttonRewind_pressed();
    void on_buttonRewind_released();
    void on_buttonForward_pressed();
    void on_buttonForward_released();
    void on_buttonEject_pressed();
    void on_buttonPlay_clicked();
    void on_buttonStop_clicked();
    void on_toolButton_clicked();
    void on_buttonPause_clicked();
};

#endif // TAPERECORDERWND_H
