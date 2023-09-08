#ifndef UTILS_H
#define UTILS_H

#include <QString>

#define _FFFF (unsigned int)(-1)

unsigned int parse_numeric_value(QString str);

unsigned int create_mask(unsigned int size, unsigned int shift);

#endif // UTILS_H
