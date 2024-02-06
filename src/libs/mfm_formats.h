#ifndef MFM_FORMATS_H
#define MFM_FORMATS_H

#include <stdint.h>

#pragma pack(1)

struct HXC_MFM_HEADER
{
    uint8_t  headername[7];          // "HXCMFM\0"

    uint16_t number_of_track;
    uint8_t  number_of_side;         // Number of elements in the MFMTRACKIMG array : number_of_track * number_of_side

    uint16_t floppyRPM;              // Rotation per minute.
    uint16_t floppyBitRate;          // 250 = 250Kbits/s, 300 = 300Kbits/s...
    uint8_t  floppyiftype;

    uint32_t mfmtracklistoffset;    // Offset of the MFMTRACKIMG array from the beginning of the file in number of uint8_ts.
};

struct HXC_MFM_TRACK_INFO
{
    uint16_t track_number;
    uint8_t  side_number;
    uint32_t mfmtracksize;          // MFM/FM Track size in bytes
    uint32_t mfmtrackoffset;        // Offset of the track data from the beginning of the file in number of bytes.
};

#pragma pack()

#endif // MFM_FORMATS_H
