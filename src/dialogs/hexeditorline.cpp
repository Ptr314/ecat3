#include "hexeditorline.h"

#include <QKeyEvent>

HexEditorLine::HexEditorLine(QWidget *parent, QFont font, unsigned int char_width, unsigned int font_height):
    QLineEdit(parent)
{
    setFixedSize(char_width*3, font_height+2);
    setAlignment(Qt::AlignHCenter);
    setFont(font);
    setStyleSheet(
        "QLineEdit {"
            "border: 1px solid white;"
            "padding: 0;"
            "margin: 0;"
            "background: #00AAAA;"
            "selection-background-color: #000080;"
        "}"
    );

    setInputMask("HH");
}

void HexEditorLine::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit return_pressed();
        return;
    } else
    if (event->key() == Qt::Key_Escape) {
        emit esc_pressed();
        return;
    };

    QLineEdit::keyPressEvent(event);
}

 bool HexEditorLine::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
        if (dynamic_cast<QKeyEvent*>(event)->key() ==  Qt::Key_Tab) {
            emit tab_pressed();
            return true;
        }
    return QLineEdit::event(event);
}
