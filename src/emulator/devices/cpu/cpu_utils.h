#ifndef CPU_UTILS_H
#define CPU_UTILS_H

#include <cstdint>

#define LO4(v)      (v & 0x0F)
#define HI4(v)      ((v >> 4) & 0x0F)

#define LO8(v)      (static_cast<uint8_t>(v & 0xFF))
#define HI8(v)      (static_cast<uint8_t>(v >> 8))

#pragma pack(1)

union PartsRecLE {
    struct{
        uint8_t L, H;
    } b;
    uint16_t w;
    uint32_t dw;
};

#pragma pack()

#endif // CPU_UTILS_H
