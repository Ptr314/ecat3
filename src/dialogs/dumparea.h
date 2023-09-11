#ifndef DUMPAREA_H
#define DUMPAREA_H

#include <QWidget>

#include "emulator/core.h"

class DumpArea : public QWidget
{
    Q_OBJECT
public:
    explicit DumpArea(QWidget *parent = nullptr);

    void set_memory(Memory * memory);

signals:

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Memory * m;
    unsigned int font_height;
    unsigned int char_width;
    unsigned int start_address;
};

#endif // DUMPAREA_H
