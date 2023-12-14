#ifndef KEYVALUEAREA_H
#define KEYVALUEAREA_H

#include <QWidget>

#include "dosframe.h"

class KeyValueArea : public DOSFrame
{
    Q_OBJECT
private:
    unsigned int font_height;
    unsigned int char_width;
    QList<QPair<QString, QString>> list;
    int key_len;
    int val_len;
    QString divider;

public:
    explicit KeyValueArea(QWidget *parent = nullptr);

    void set_data(QList<QPair<QString, QString>> newlist);
    void set_divider(QString new_divider);

protected:
    void paintEvent(QPaintEvent *event) override;

signals:

};

#endif // KEYVALUEAREA_H
