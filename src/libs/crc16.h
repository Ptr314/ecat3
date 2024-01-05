#ifndef CRC16_H
#define CRC16_H

#include <cstdint>

void CRC16_update(uint16_t * CRC, uint8_t * buffer, unsigned int len);
uint16_t CRC16(uint8_t * buffer, unsigned int len);

#endif // CRC16_H
