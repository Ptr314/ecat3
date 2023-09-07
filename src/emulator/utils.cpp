#include <QException>

#include "utils.h"

int parse_numeric_value(QString str)
{
    int base;
    int mult;
    bool valid;

    if (str.isEmpty()) throw QException();

    QString s = str.toUpper();

    QChar first = s.at(0);
    if (first=='$') base=16;
    else if (first=='#') base=2;
    else base = 10;

    if (base!=10) s.remove(0, 1);

    if (s.at(s.length()-1) == 'K'){
        mult = 1024;
        s.remove(s.length()-1, 1);
    } else
        mult = 1;

    int value = s.toInt(&valid, base);

    if (!valid) throw QException();

    return value*mult;
}
