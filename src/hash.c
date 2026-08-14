// This code is based off of the SHA-256 specifications from RFC-6234.
// It is not derived from the C source code present in the document.
#include <stdint.h>
#include "hash.h"

#define NULL ((void *)0)

uint64_t solveK(uint64_t msgBytes)
{
        // Getting length parameters by solving ( lBits + 1 + kBits ) mod 512 = 448
        const uint64_t lBits = 8 * msgBytes;
        const uint16_t lBitsMod512 = lBits % 512;
        uint64_t kBits = 447;

        if (lBitsMod512 > kBits) {
                kBits += 512;
        }

        kBits -= lBitsMod512;
        return kBits;
}

// Gets the length that a message of `msgBytes` octets would be padded to for 
// SHA-256 hashing. The return value is in bytes, not bits! If msgBytes is too large 
// (>= 2**61), this returns 0; this is the only error that can occur
uint64_t paddedLen(uint64_t msgBytes) 
{
        if (msgBytes >= (1uLL << 61)) {
                return 0;
        }

        const uint64_t kBits = solveK(msgBytes); // (kBits mod 8) ALWAYS equals 7
        const uint64_t paddingBytes = 8 + (kBits + 1) / 8;
        const uint64_t finalLenBytes = msgBytes + paddingBytes;

        if (kBits % 8 != 7 || finalLenBytes % 64 != 0) {
                // These should NEVER happen, but we check just in case
                return 0;
        }

        return finalLenBytes;
}

uint64_t padMsg(uint8_t *outBuf, uint64_t outBytes, const uint8_t *msg, 
        uint64_t msgBytes) 
{
        if (outBuf == NULL 
                || msg == NULL 
                || outBytes < msgBytes 
                || msgBytes >= (1uLL << 61)
        ) {
                return 0;
        }

        // Getting the padded length, and verifying that outBytes is large enough
        const uint64_t finalLenBytes = paddedLen(msgBytes);
        const uint64_t paddingBytes = finalLenBytes - msgBytes;

        if (finalLenBytes > outBytes) {
                // Our padded message won't fit
                printf("Not enough room. Need %llu; got %llu\n", finalLenBytes, outBytes);
                return 0;
        }

        // Padding with the length of the message, in little endian
        for (uint8_t i = 0; i < 8; i++) {
                outBuf[i] = (msgBytes & (0xff << (8 * i))) >> (8 * i);
        }

        // Padding a whole bunch of zeros, and the 1 at the end
        for (uint64_t i = 8; i < paddingBytes; i++) {
                outBuf[i] = 0;
        }

        outBuf[paddingBytes - 1] = 0x80u; // 0b1000_0000

        // Appending our actual message
        for (uint64_t i = paddingBytes; i < finalLenBytes; i++) {
                outBuf[i] = msg[i - paddingBytes];
        }

        return finalLenBytes;
}