// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Qt-dependent utility functions (not part of the emulator core)

#include "qt_utils.h"

#define MD4C_USE_UTF8
#include "libs/md4c/md4c-html.h"

QString pad_string(QString s, QChar c, int len, bool from_left)
{
    QString v = s;
    for (int i=0; i<len - s.length(); i++)
        if (from_left) v = c + v;
        else v = v + c;
    return v;
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

static void store_html_callback(const MD_CHAR* text, MD_SIZE size, void* result)
{
    static_cast<std::string*>(result)->append(text, size);
}

std::string md2html(const std::string &md)
{
    std::string result;
    md_html(md.c_str(), md.length(), store_html_callback, static_cast<void*>(&result), 0, 1);
    return result;
}