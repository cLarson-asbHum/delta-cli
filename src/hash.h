// hash.h
//
// Methods for creating, grabbing, and verifying hashes

#ifndef HASH_H
#define HASH_H

#include <stdio.h>

// A SHA that is used as a placeholder. A warning should be issued if
// a null SHA is detected and the --ignore-hash flag is not present. An
// error must be thrown if a null sha collision is *generated*
#define NULL_SHA { .bytes = { 0xE1u,0x6Bu,0x57u,0xA5u,0x88u,0x03u,0xE4u,0x3Cu,\
                              0xBEu,0x51u,0x95u,0x8Fu,0x62u,0x08u,0x27u,0x58u,\
                              0xA1u,0xA5u,0x0Du,0xA5u,0xCEu,0xF7u,0xE3u,0x88u,\
                              0x69u,0x4Du,0xB8u,0xF1u,0x24u,0x3Fu,0x1Cu,0x40u } }


// Little-endian 256-bit digest of a SHA-2 hash
union Sha256 {
        uint8_t bytes[32];
        uint64_t longs[4];
};

// Writes the sha256 digest in little-endian format to the given stream.
// This returns the same status as `fwrite()` from `<stdio.h>`
int writeSha(FILE *outStream, const union Sha256 *sha256);

// Reads a little-endian SHA-256 digest from the specified stream stored as 
// arbitrary binary, writing the output to the specified Sha256 pointer. This 
// will attempt to read exactly 32 bytes from the stream, starting at the file's 
// current position. If the end of the file is reached (or an error occurs while 
// reading), this will read fewer bytes.
//
// The return value is the number of bytes read. If all goes well, this returns
// 32. It must **never** return more than 32, or fewer than 0.
int readBinarySha(const FILE *inStream, union Sha256 *out);

// Reads a little-endian SHA-256 digest from the specified stream stored as 
// printable hex digits, writing the output to the specified Sha256 pointer. This 
// will attempt to read exactly 64 bytes from the stream, starting at the file's 
// current position. If the end of the file is reached (or an error occurs while 
// reading), this will read fewer bytes.
//
// The return value is the number of bytes read. If all goes well, this returns
// 64. It must **never** return more than 64, or fewer than 0.
int readHexSha(const FILE *asciiHex, union Sha256 *out);

#endif 