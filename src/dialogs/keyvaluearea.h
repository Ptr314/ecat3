#ifndef KEYVALUEAREA_H
#define KEYVALUEAREA_H

#include <QWidget>

class KeyValueArea : public QWidget
{
    Q_OBJECT
private:
    unsigned int font_height;
    unsigned int char_width;
    QList<QString> list;

public:
    explicit KeyValueArea(QWidget *parent = nullptr);

    void set_data(QList<QString> newlist);

protected:
    void paintEvent(QPaintEvent *event) override;

signals:

};

#endif // KEYVALUEAREA_H
