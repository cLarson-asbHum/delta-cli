// hash.h
//
// Methods for creating, grabbing, and verifying hashes

#ifndef HASH_H
#define HASH_H

#include <stdio.h>

// A SHA that is used as a placeholder. A warning should be issued if
// a null SHA is detected and the --ignore-hash flag is not present. An
// error must be thrown if a null sha collision is *generated*
#define NULL_HASH { .longs = {0,0,0,0} }

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