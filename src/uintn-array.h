#ifndef UINTN_ARRAY_H
#define UINTN_ARRAY_H

#include <stdint.h>

// Specifies an array of uints whose byte width is specified by the parameter.
// The length of the array itself is not self-specified.
struct UintNArray {
        uint8_t *bytes; // Number of bytes must be a multiple of byteWidth
        uint8_t byteWidth; // How many bytes each uint element takes up. Min=1; max=8
};

// Calculates the minimum number of bytes necessary to store the given value. 
// The return value is **always** between 1 and 8, inclusive. If x is 0, this 
// returns 1.
uint8_t calcWidth(uint64_t x);

// Reads a uint from the specified UintNArray. Return is undefined if the index
// is out of bounds or if the byteWidth is not between 1 and 8 (inclusive).
// i is the uint index, not the byte index (i.e. `bytes[byteWidth * i]` rather 
// than `bytes[i]`). This assumes that the bytes are in little-endian order
uint64_t readUint(const struct UintNArray *arr, uint64_t i);


// Writes a uint val to the specified UintNArray, returning the number of bytes 
// written. The bytes of val are written in little-endian order. 
// 
// Behavior is undefined if the index is out of bounds or if the byteWidth is 
// not between 1 and 8 (inclusive). i is the uint index, not the byte index 
// (i.e. `bytes[byteWidth * i]` rather  than `bytes[i]`).
uint8_t writeUint(struct UintNArray *arr, uint64_t i, uint64_t val);

#endif