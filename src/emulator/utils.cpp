#include <QException>

#include "utils.h"

unsigned int parse_numeric_value(QString str)
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

unsigned int create_mask(unsigned int size, unsigned int shift)
{
    return ~(_FFFF << size) << shift;
    // (4, 4):
    //1                    FFFF
    //2                            FFF0
    //3    000F
    //4                                     00F0
}

void convert_range(QString s, unsigned int * v1, unsigned int * v2)
{
    if (!s.isEmpty())
    {
        int p = s.indexOf('-');
        if (p<0)
        {
            *v1 = parse_numeric_value(s);
            *v2 = *v1;
        } else {
            *v1 = parse_numeric_value(s.left(p));
            *v2 = parse_numeric_value(s.right(s.length()-p-1));
        }
    } else
        throw QException();
}

unsigned int CalcBits(unsigned int V, unsigned int MaxBits)
{
    unsigned int result = 0;
    for (unsigned int i=0; i < MaxBits-1; i++)
        result += (V >> i) & 1;
    return result;
}
