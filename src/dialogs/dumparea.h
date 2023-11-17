#ifndef DUMPAREA_H
#define DUMPAREA_H

#include <QWidget>
#include <QLineEdit>

#include "emulator/emulator.h"

class DumpArea : public QWidget
{
    Q_OBJECT
public:
    explicit DumpArea(QWidget *parent = nullptr);

    void set_data(Emulator * e, AddressableDevice * device, unsigned int start_address = 0);
    void go_to(unsigned int address);

signals:

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    AddressableDevice * d;
    Emulator * e;
    QLineEdit * editor;
    unsigned int font_height;
    unsigned int char_width;
    unsigned int start_address;
};

#endif // DUMPAREA_H
