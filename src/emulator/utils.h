#pragma once

#include <QString>
#ifdef RENDERER_SDL2
    #include <SDL.h>
#endif

#include "emulator/config.h"
#include "emulator/core.h"

#define _FFFF (unsigned int)(-1)

unsigned int parse_numeric_value(QString str);

unsigned int create_mask(unsigned int size, unsigned int shift);

void convert_range(QString s, unsigned int * v1, unsigned int * v2);

unsigned int CalcBits(unsigned int V, unsigned int MaxBits = 32);

bool fileExists(QString path);

unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp);

QString pad_string(QString s, QChar c, int len, bool from_left = true);

QString find_file_location(SystemData * sd, QString file_name);

unsigned int read_confg_value(EmulatorConfigDevice * cd, QString name, bool required, unsigned int def);
QString read_confg_value(EmulatorConfigDevice * cd, QString name, bool required, QString def);
bool read_confg_value(EmulatorConfigDevice * cd, QString name, bool required, bool def);

bool checkCapsLock();

QString md2html(QString md);

int getRandomNumber(int min, int max);

