#ifndef HEXEDITORLINE_H
#define HEXEDITORLINE_H

#include <QLineEdit>

class HexEditorLine : public QLineEdit
{
public:
    HexEditorLine(QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *event) override;

};

#endif // HEXEDITORLINE_H
