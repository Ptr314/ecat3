#ifndef HEXEDITORLINE_H
#define HEXEDITORLINE_H

#include <QLineEdit>

class HexEditorLine : public QLineEdit
{
    Q_OBJECT
public:
    HexEditorLine(QWidget *parent);
    HexEditorLine(QWidget *parent, QFont font, unsigned int char_width, unsigned int font_height);

signals:
    void tab_pressed();
    void return_pressed();
    void esc_pressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
};

#endif // HEXEDITORLINE_H
