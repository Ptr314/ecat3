#ifndef DISASMAREA_H
#define DISASMAREA_H

#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/disasm.h"
#include "dosframe.h"

struct DisAsmEntry {
    unsigned int address;
    unsigned int len;
    QString code;
    QString command;
    bool breakpoint;
    bool current;
};

class DisAsmArea : public DOSFrame
{
    Q_OBJECT
public:
    explicit DisAsmArea(QWidget *parent = nullptr);

    void set_data(Emulator * e, CPU * cpu, DisAsm * disasm, unsigned int address);
    void go_to(unsigned int address);
    unsigned int get_address_at_cursor();
    void invalidate();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    Emulator * e;
    CPU * cpu;
    DisAsm * disasm;
    uint16_t CRC;

    unsigned int font_height;
    unsigned int char_width;
    unsigned int address_first;
    unsigned int address;
    unsigned int address_last;
    unsigned int lines_count;
    DisAsmEntry lines[100];
    unsigned int max_lines;
    unsigned int screen_size;
    unsigned int first_line;
    unsigned int cursor_line;
    bool data_valid;

    void update_data();

signals:

};

#endif // DISASMAREA_H
