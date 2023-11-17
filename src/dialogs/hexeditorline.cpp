#include "hexeditorline.h"

#include <QKeyEvent>

HexEditorLine::HexEditorLine(QWidget *parent):
    QLineEdit(parent)
{

}

void HexEditorLine::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "keypressEvent";

    if (event->key() == Qt::Key_Return) {
        qDebug() << "keypressEvent Return";
        return;
    }
    QLineEdit::keyPressEvent(event);
}
