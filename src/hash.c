// This code is based off of the SHA-256 specifications from RFC-6234.
// It is not derived from the C source code present in the document.
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log-level.h"
#include "hash.h"

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
                return 0;
        }

        // Writing the message as-is first
        for (uint64_t i = 0; i < msgBytes; i++) {
                outBuf[i] = msg[i];
        }

        // Appending a 1 bit to the end of the message (equivalent to 0x80 after it)
        outBuf[msgBytes] = 0x80u;

        // Writing the myriad of zeros
        for (uint64_t i = msgBytes + 1; i < finalLenBytes - 8; i++) {
                outBuf[i] = 0;
        }

        // Writing the length in big endian
        for (uint64_t i = finalLenBytes - 8; i < finalLenBytes; i++) {
                const uint8_t shift = 8 * (i - finalLenBytes + 8);
                outBuf[i] = ((8 * msgBytes) & (0xff << (56 - shift))) >> (56 - shift);
        }
        return finalLenBytes;
}

uint64_t padBlock(uint8_t *outBuf, uint64_t outBytes, 
        const uint8_t *partialBlock, uint64_t msgBytes) 
{
        const uint64_t partialBlockBytes = msgBytes % 64;
        if (outBuf == NULL 
                || partialBlock == NULL 
                || outBytes < partialBlockBytes 
                || msgBytes >= (1uLL << 61)
        ) {
                return 0;
        }

        // Getting the padded length, and verifying that outBytes is large enough
        const uint64_t paddingBytes = paddedLen(msgBytes) - msgBytes;
        const uint64_t finalLenBytes = partialBlockBytes + paddingBytes;

        if (finalLenBytes > outBytes) {
                // Our padded message won't fit
                return 0;
        }

        // Writing the message with a 1 bit after it 
        memcpy(outBuf, partialBlock, partialBlockBytes);
        outBuf[partialBlockBytes] = 0x80u;

        // Writing the myriad of zeros
        for (uint64_t i = partialBlockBytes + 1; i < finalLenBytes - 8; i++) {
                outBuf[i] = 0;
        }

        // Writing the length in big endian
        for (uint8_t i = finalLenBytes - 8; i < finalLenBytes; i++) {
                const uint8_t shift = 56 - 8 * (i - finalLenBytes + 8);
                outBuf[i] = ((8 * msgBytes) & (0xff << shift)) >> shift;
        }
        return finalLenBytes;
}

uint32_t rotr(uint32_t x, uint8_t n) 
{
        return (x >> n) | ((((1 << n) - 1) & x) << (32 - n));
}

uint32_t ch(uint32_t x, uint32_t y, uint32_t z) 
{
        return (x & y) ^ (~x & z);
}

uint32_t maj(uint32_t x, uint32_t y, uint32_t z) 
{
        return (x & y) ^ (x & z) ^ (y & z);
}

uint32_t bsig0(uint32_t x) 
{
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

uint32_t bsig1(uint32_t x) 
{
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

uint32_t ssig0(uint32_t x) 
{
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

uint32_t ssig1(uint32_t x) 
{
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// Arbitrary constants described in RFC 6234 section 5.1
const uint32_t K[] = PRIME_CBRTS;

// Reads the i-th 32 bit unsigned big-endian integer stored in the buffer. 
// Essentially, this converts from a big endian integer into a system-endianness
// integer.
uint32_t readBigEnd(const uint8_t *buf, uint64_t start) 
{
        uint32_t total = 0x00000000;
        for (uint8_t i = 0; i < 4; i++) {
                total = total | (buf[4 * start + i] << (24 - 8 * i));
        }
        return total;
}

void writeBigEnd(uint8_t *buf, uint64_t start, uint32_t val) 
{
        for (uint8_t i = 0; i < 4; i++) {
                buf[4 * start + i] = (val & (0xff << (24 - 8 * i))) >> (24 - 8 * i);
        }
}

uint8_t computeBlockHash(union Sha256 *dest, const uint8_t block[64]) 
{
        if (dest == NULL || block == NULL) {
                return 0;
        }

        // Preparing the message schedule
        uint32_t w[64];
        for (uint8_t i = 0; i < 16; i++) {
                w[i] = readBigEnd(block, i);
        }

        for(uint8_t i = 16; i < 64; i++) {
                w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
        }

        // Initialize the working variables
        uint32_t a = readBigEnd(dest->bytes, 0);
        uint32_t b = readBigEnd(dest->bytes, 1);
        uint32_t c = readBigEnd(dest->bytes, 2);
        uint32_t d = readBigEnd(dest->bytes, 3);
        uint32_t e = readBigEnd(dest->bytes, 4);
        uint32_t f = readBigEnd(dest->bytes, 5);
        uint32_t g = readBigEnd(dest->bytes, 6);
        uint32_t h = readBigEnd(dest->bytes, 7);

        // Main hash computation
        for (uint8_t i = 0; i < 64; i++) {
                const uint32_t tmp1 = h + bsig1(e) + ch(e, f, g) + K[i] + w[i];
                const uint32_t tmp2 = bsig0(a) + maj(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + tmp1;
                d = c;
                c = b;
                b = a;
                a = tmp1 + tmp2;
        }

        // Finalizing the intermediate hash value (the result for this function):
        writeBigEnd(dest->bytes, 0, a + readBigEnd(dest->bytes, 0));
        writeBigEnd(dest->bytes, 1, b + readBigEnd(dest->bytes, 1));
        writeBigEnd(dest->bytes, 2, c + readBigEnd(dest->bytes, 2));
        writeBigEnd(dest->bytes, 3, d + readBigEnd(dest->bytes, 3));
        writeBigEnd(dest->bytes, 4, e + readBigEnd(dest->bytes, 4));
        writeBigEnd(dest->bytes, 5, f + readBigEnd(dest->bytes, 5));
        writeBigEnd(dest->bytes, 6, g + readBigEnd(dest->bytes, 6));
        writeBigEnd(dest->bytes, 7, h + readBigEnd(dest->bytes, 7));
        return 1;
}

uint8_t initSha256(union Sha256 *dest) 
{
        if (dest == NULL) {
                return 0;
        }

        writeBigEnd(dest->bytes,0, (uint32_t) 0x6a09e667u);
        writeBigEnd(dest->bytes,1, (uint32_t) 0xbb67ae85u);
        writeBigEnd(dest->bytes,2, (uint32_t) 0x3c6ef372u);
        writeBigEnd(dest->bytes,3, (uint32_t) 0xa54ff53au);
        writeBigEnd(dest->bytes,4, (uint32_t) 0x510e527fu);
        writeBigEnd(dest->bytes,5, (uint32_t) 0x9b05688cu);
        writeBigEnd(dest->bytes,6, (uint32_t) 0x1f83d9abu);
        writeBigEnd(dest->bytes,7, (uint32_t) 0x5be0cd19u);
        return 1;
}

uint64_t computeHash(union Sha256 *dest,  const uint8_t *msg, 
        const uint64_t msgBytes) 
{
        if (dest == NULL || msg == NULL || msgBytes >= UINT64_MAX / 8) {
                return 0;
        }

        // Padding the message
        const uint64_t padLen = paddedLen(msgBytes);
        if (padLen == 0) {
                // Should NEVER happen, but just in case
                return 0;
        }

        uint8_t *padded = malloc(padLen);
        if (padded == NULL || padMsg(padded, padLen, msg, msgBytes) == 0) {
                free(padded);
                return 0;
        }

        // Computing the hash over every 512 bit block
        initSha256(dest);
        uint64_t ret = 0;
        for (uint64_t i = 0; i < padLen; i += 64) {
                if (computeBlockHash(dest, &padded[i]) == 0) {
                        free(padded);
                        return 0;
                }
                ret++;
        }

        free(padded);
        return ret;
}

uint8_t hashesEqual(const union Sha256 *h1, const union Sha256 *h2) 
{
        for (uint8_t i = 0; i < 8; i++) {
                if (h1->words32[i] != h2->words32[i]) {
                        return 0;
                }
        }

        return 1;
}

uint64_t computeFileHash(union Sha256 *dest, FILE *file) 
{
        if (dest == NULL || file == NULL) {
                error("Error while hashing file: The allocated hash destination or the file itself was null\n");
                detail(" \\__ Hash Destination Address: %p\n", dest);
                detail(" \\__ File Address: %p\n", file);
                return 0;
        }

        uint64_t ret = 0;
        uint64_t msgLen = 0;
        initSha256(dest);
        rewind(file);
        while (!feof(file)) {
                // Reading a single block from the file
                uint8_t rawBlock[64];
                const uint64_t bytesRead = fread(rawBlock, 1, 64, file);
                msgLen += bytesRead;

                if (ferror(file) || (bytesRead < 64 && !feof(file))) {
                        error("Error while hashing file: Unable to read block %llu\n", 
                                ret);
                        loud(" \\___ Reason: %s\n", strerror(ferror(file)));
                        return 0;
                }

                // Padding the read block if it is the last one
                uint8_t block[128];
                uint64_t blockLength = 64; // Might change when padded
                memcpy(block, rawBlock, bytesRead);

                if (bytesRead < 64) {
                        blockLength = padBlock(block, 128, rawBlock, msgLen);
                } 

                if (blockLength == 0) {
                        // Should never happen, but just in case
                        error("Error while hashing file: Unable to pad block %llu\n", 
                                ret);
                        return 0;
                }

                for (uint64_t i = 0; i < blockLength; i += 64) {
                        computeBlockHash(dest, &block[i]);
                        ret++;
                }
        }

        // Verifying that this is NOT the null hash
        const union Sha256 nullHash = NULL_SHA;
        if (hashesEqual(dest, &nullHash)) {
                error("Garbage state: The produced hash collided with the designated null hash\n");
                detail(" \\___ Null hash: 0x ");
                for (uint8_t i = 0; i < 8; i++) {
                        detail(" %08llx", nullHash.words32[i]);
                }
                detail("\n");
                return 0;
        }

        return ret;
}