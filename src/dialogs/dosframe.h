#ifndef DOSFRAME_H
#define DOSFRAME_H

#include <QObject>
#include <QWidget>

class DOSFrame : public QWidget
{
    Q_OBJECT

private:
    QString frame_chars;
    bool frame_top;
    bool frame_right;
    bool frame_bottom;
    bool frame_left;

public:
    explicit DOSFrame(QWidget *parent = nullptr);
    void set_frame(bool top, bool right, bool bottom, bool left, QString chars="╔═╗║ ║╚═╝");

protected:
    QFont * font;
    unsigned int font_height;
    unsigned int  char_width;

    void paintEvent(QPaintEvent *event) override;


};

#endif // DOSFRAME_H
