#include <QPainter>

#include "dialogs/dialogs.h"
#include "keyvaluearea.h"
#include "emulator/utils.h"

KeyValueArea::KeyValueArea(QWidget *parent)
    : DOSFrame{parent}
{
//    QFont font(FONT_NAME, FONT_SIZE);
    divider = ": ";
    QFontMetrics fm(*font);
    #if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
        char_width = fm.width('0');
    #else
        char_width = fm.horizontalAdvance('0');
    #endif
    font_height = fm.height();
}

void KeyValueArea::set_data(QList<QPair<QString, QString>> newlist)
{
    list = newlist;

    key_len = 0;
    val_len = 0;
    QListIterator<QPair<QString, QString>> i(list);
    while (i.hasNext()) {
        QPair<QString, QString> p = i.next();
        if (p.first.length() > key_len) key_len = p.first.length();
        if (p.second.length() > val_len) val_len = p.second.length();
    }

    update();
}

void KeyValueArea::set_divider(QString new_divider)
{
    divider = new_divider;
}

void KeyValueArea::paintEvent(QPaintEvent *event)
{
    DOSFrame::paintEvent(event);

    QPainter painter(this);
    //painter.fillRect(0, 0, size().width(), size().height(), DIALOGS_BACKGROUND);
    painter.setFont(*font);

    painter.setPen(TEXT_COLOR);

    unsigned int x = ((size().width() / char_width - key_len - divider.length() - val_len) / 2) * char_width;

    QListIterator<QPair<QString, QString>> i(list);
    int c = 2;
    while (i.hasNext()) {
        QPair<QString, QString> p = i.next();
        unsigned int y = font_height * c;
        if (p.first.at(0) != '-')
            painter.drawText(x, y, pad_string(p.first, ' ', key_len) + divider + p.second );
        //qDebug() << c << i.key() << i.value();
        c++;
    }
}
