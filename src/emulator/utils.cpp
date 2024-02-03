#include <QException>
#include <QFileInfo>
#include <QDir>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

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

bool fileExists(QString path) {
    QFileInfo check_file(path);
    if (check_file.exists() && check_file.isFile()) {
        return true;
    } else {
        return false;
    }
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

QString find_file_location(QString system_path, QString software_path, QString file_name)
{
    if (!file_name.isEmpty())
    {
        QString dir = QFileInfo(system_path).dir().dirName();
        QString file;

        file = system_path + file_name;
        if (QFile::exists(file)) return file;

        file = system_path + "files/" + file_name;
        if (QFile::exists(file)) return file;

        file = software_path + file_name;
        if (QFile::exists(file)) return file;

        file = software_path + dir + "/" + file_name;
        if (QFile::exists(file)) return file;
    }
    return "";
}


void fill_SDL_rgba(const uint8_t colors[][3], uint32_t * RGBA, int len, const SDL_PixelFormat * format)
{
    for (int i=0; i<len; i++)
        RGBA[i] = SDL_MapRGB(format, colors[i][0], colors[i][1], colors[i][2]);
}

unsigned int read_confg_value(EmulatorConfigDevice * cd, QString name, bool required, unsigned int def)
{
    QString s = cd->get_parameter(name, required).value;
    if (s.isEmpty()) {
        return def;
    } else {
        return parse_numeric_value(s);
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
