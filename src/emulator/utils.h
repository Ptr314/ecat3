#ifndef UTILS_H
#define UTILS_H

#include <QString>

#define _FFFF (unsigned int)(-1)

unsigned int parse_numeric_value(QString str);

unsigned int create_mask(unsigned int size, unsigned int shift);

void convert_range(QString s, unsigned int * v1, unsigned int * v2);

unsigned int CalcBits(unsigned int V, unsigned int MaxBits = 32);

bool fileExists(QString path);

unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp);

QString pad_string(QString s, QChar c, int len, bool from_left = true);

QString find_file_location(QString system_path, QString software_path, QString file_name);

#endif // UTILS_H
