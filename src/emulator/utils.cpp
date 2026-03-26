// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Service functions, source

#include <algorithm>
#include <random>
#include <sstream>
#include <stdexcept>

#include <QFileInfo>
#include <QDir>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#include "utils.h"
#include "dsk_tools/dsk_tools.h"

#define MD4C_USE_UTF8
#include "libs/md4c/md4c-html.h"

std::vector<std::string> split_string(const std::string &s, char delimiter, bool skip_empty)
{
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delimiter))
    {
        if (!skip_empty || !token.empty())
            result.push_back(token);
    }
    return result;
}

std::string str_trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string str_tolower(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string hex_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*X", width, value);
    return std::string(buf);
}

unsigned int parse_numeric_value(QString str)
{
    int base;
    int mult;
    bool valid;

    if (str.isEmpty()) throw std::runtime_error("Empty numeric value");

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

    if (!valid) throw std::runtime_error("Invalid numeric value: " + str.toStdString());

    return value*mult;
}

unsigned int parse_numeric_value(std::string str)
{
    int base;
    int mult;

    if (str.empty())
        throw std::invalid_argument("Empty numeric value");

    std::string s = str;
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(s[i]);

    char first = s[0];
    if (first == '$') base = 16;
    else if (first == '#') base = 2;
    else base = 10;

    if (base != 10) s.erase(0, 1);

    if (s[s.length() - 1] == 'K') {
        mult = 1024;
        s.erase(s.length() - 1, 1);
    } else {
        mult = 1;
    }

    char *end;
    long value = strtol(s.c_str(), &end, base);

    if (*end != '\0') throw std::invalid_argument("Invalid numeric value: " + str);

    return value * mult;
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

void convert_range(const std::string &s, unsigned int * v1, unsigned int * v2)
{
    if (!s.empty())
    {
        size_t p = s.find('-');
        if (p == std::string::npos)
        {
            *v1 = parse_numeric_value(s);
            *v2 = *v1;
        } else {
            *v1 = parse_numeric_value(s.substr(0, p));
            *v2 = parse_numeric_value(s.substr(p + 1));
        }
    } else
        throw std::runtime_error("Empty range value");
}

unsigned int CalcBits(unsigned int V, unsigned int MaxBits)
{
    unsigned int result = 0;
    for (unsigned int i=0; i < MaxBits-1; i++)
        result += (V >> i) & 1;
    return result;
}

unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp) {

    // Copyright (c) 2005-2010 Lode Vandevenne
    // https://github.com/lvandeve/lodepng/blob/master/examples/example_bmp2png.cpp

    static const unsigned MINHEADER = 54; //minimum BMP header size

    if(bmp.size() < MINHEADER) return -1;
    if(bmp[0] != 'B' || bmp[1] != 'M') return 1; //It's not a BMP file if it doesn't start with marker 'BM'
    unsigned pixeloffset = bmp[10] + 256 * bmp[11]; //where the pixel data starts
    //read width and height from BMP header
    w = bmp[18] + bmp[19] * 256;
    h = bmp[22] + bmp[23] * 256;
    //read number of channels from BMP header
    if(bmp[28] != 24 && bmp[28] != 32) return 2; //only 24-bit and 32-bit BMPs are supported.
    unsigned numChannels = bmp[28] / 8;

    //The amount of scanline bytes is width of image times channels, with extra bytes added if needed
    //to make it a multiple of 4 bytes.
    unsigned scanlineBytes = w * numChannels;
    if(scanlineBytes % 4 != 0) scanlineBytes = (scanlineBytes / 4) * 4 + 4;

    unsigned dataSize = scanlineBytes * h;
    if(bmp.size() < dataSize + pixeloffset) return 3; //BMP file too small to contain all pixels

    image.resize(w * h * 4);

    /*
  There are 3 differences between BMP and the raw image buffer for LodePNG:
  -it's upside down
  -it's in BGR instead of RGB format (or BRGA instead of RGBA)
  -each scanline has padding bytes to make it a multiple of 4 if needed
  The 2D for loop below does all these 3 conversions at once.
  */
    for(unsigned y = 0; y < h; y++)
        for(unsigned x = 0; x < w; x++) {
            //pixel start byte position in the BMP
            unsigned bmpos = pixeloffset + (h - y - 1) * scanlineBytes + numChannels * x;
            //pixel start byte position in the new raw image
            unsigned newpos = 4 * y * w + 4 * x;
            if(numChannels == 3) {
                image[newpos + 0] = bmp[bmpos + 2]; //R
                image[newpos + 1] = bmp[bmpos + 1]; //G
                image[newpos + 2] = bmp[bmpos + 0]; //B
                image[newpos + 3] = 255;            //A
            } else {
                image[newpos + 0] = bmp[bmpos + 2]; //R
                image[newpos + 1] = bmp[bmpos + 1]; //G
                image[newpos + 2] = bmp[bmpos + 0]; //B
                image[newpos + 3] = bmp[bmpos + 3]; //A
            }
        }
    return 0;
}

QString pad_string(QString s, QChar c, int len, bool from_left)
{
    QString v = s;
    for (int i=0; i<len - s.length(); i++)
        if (from_left) v = c + v;
        else v = v + c;
    return v;
}

std::string find_file_location(SystemData * sd, const std::string &file_name)
{
    if (!file_name.empty())
    {
        const std::string &system_path = sd->system_path;
        const std::string &software_path = sd->software_path;
        const std::string &data_path = sd->data_path;
        std::string dir = dsk_tools::parent_dir_name(system_path);
        std::string file;

        file = system_path + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = system_path + "files/" + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = software_path + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = software_path + dir + "/" + file_name;
        if (dsk_tools::file_exists(file)) return file;

        file = data_path + file_name;
        if (dsk_tools::file_exists(file)) return file;
    }
    return "";
}

unsigned int read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, unsigned int def)
{
    std::string s = cd->get_parameter(name, required).value;
    if (s.empty()) {
        return def;
    } else {
        return parse_numeric_value(s);
    }
}

std::string read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, const std::string &def)
{
    std::string s = cd->get_parameter(name, required).value;
    if (s.empty()) {
        return def;
    } else {
        return str_tolower(s);
    }
}

bool read_confg_value(EmulatorConfigDevice * cd, const std::string &name, bool required, bool def)
{
    std::string s = str_tolower(cd->get_parameter(name, required).value);
    if (s.empty()) {
        return def;
    } else {
        if (s == "1" || s == "true" || s == "y" || s == "yes") return true;
        if (s == "0" || s == "false" || s == "n" || s == "no") return false;
        throw std::runtime_error("Invalid boolean value");
    }
}

bool checkCapsLock()
{
// https://www.qtcentre.org/threads/30180-how-to-determine-if-CapsLock-is-on-crossplatform
#ifdef Q_OS_WIN32 // MS Windows version
    return GetKeyState(VK_CAPITAL) == 1;
#else
    return false;
#endif
}

void store_html_callback(const MD_CHAR* text, MD_SIZE size, void* result)
{
    static_cast<std::string*>(result)->append(text, size);
}

std::string md2html(const std::string &md)
{
    std::string result;
    md_html(md.c_str(), md.length(), store_html_callback, static_cast<void*>(&result), 0, 1);
    return result;
}

int getRandomNumber(int min, int max) {
    static std::random_device rd;  // Источник случайности
    static std::mt19937 gen(rd()); // Генератор
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}
