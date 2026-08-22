#include <stdint.h>
#include "uintn-array.h"

#define READ_64(X) ( *((uint64_t *) &(X)) )

uint8_t calcWidth(uint64_t x) 
{
        for (uint8_t byteShift = 1; byteShift < 8; byteShift++) {
                if (x < (1 << (8 * byteShift))) {
                        return byteShift;
                }
        }

        return 8;
}

uint64_t readUint(const struct UintNArray *arr, uint64_t i) 
{
        const uint8_t w = arr->byteWidth;
        return READ_64(arr->bytes[w * i]) & ((1 << (8 * w)) - 1);
}

uint8_t writeUint(struct UintNArray *arr, uint64_t i, uint64_t val) 
{
        const uint8_t w = arr->byteWidth;
        for (uint64_t j = 0; j < w; j++) {
                arr->bytes[w * i + j] = (val >> (8 * j)) & 0xff;
        }
        return w;
}