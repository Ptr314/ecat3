#ifndef DISASMAREA_H
#define DISASMAREA_H

#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/disasm.h"

struct DisAsmEntry {
    unsigned int address;
    QString code;
    QString command;
    bool breakpoint;
    bool current;
};

class DisAsmArea : public QWidget
{
    Q_OBJECT
public:
    explicit DisAsmArea(QWidget *parent = nullptr);

    void set_data(Emulator * e, CPU * cpu, DisAsm * disasm, unsigned int address);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Emulator * e;
    CPU * cpu;
    DisAsm * disasm;

    unsigned int font_height;
    unsigned int char_width;
    unsigned int address_first;
    unsigned int address_last;
    unsigned int lines_count;
    DisAsmEntry lines[100];
    unsigned int max_lines;

    void go_to(unsigned int address);

signals:

};

#endif // DISASMAREA_H
