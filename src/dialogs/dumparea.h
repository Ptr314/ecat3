#ifndef DUMPAREA_H
#define DUMPAREA_H

#include <QWidget>
#include <QLineEdit>

#include "emulator/emulator.h"
#include "dosframe.h"

class DumpArea : public DOSFrame
{
    Q_OBJECT
public:
    explicit DumpArea(QWidget *parent = nullptr);

    void set_data(Emulator * e, AddressableDevice * device, unsigned int start_address = 0);
    void go_to(int address);
    void go_to(int offset, int address);
    void page_down();
    void page_up();
    void update_view();
    void reset_buffer();

signals:

protected:
    void paintEvent(QPaintEvent *event) override;
    unsigned int mouse_to_offset(unsigned int  x, unsigned int y);
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    AddressableDevice * d;
    Emulator * e;
    QLineEdit * editor;
//    unsigned int font_height;
//    unsigned int char_width;
    unsigned int global_offset;
    unsigned int start_address;
    unsigned int editor_address;
    unsigned int hilight_address;
    unsigned int lines_count;
    unsigned int data_size;
    unsigned int scroll_size;

    uint8_t buffer[2][64*16];
    unsigned int current_buffer;
    bool buffer_is_valid;
    void fill_buffer(bool prefill=false);

    void show_editor(unsigned int address);

private slots:
    void editor_return_pressed();
    void editor_tab_pressed();
    void editor_escape_pressed();
};

#endif // DUMPAREA_H
