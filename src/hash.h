// hash.h
//
// Methods for creating, grabbing, and verifying hashes

#ifndef HASH_H
#define HASH_H

#include <stdint.h>
#include <stdio.h>

// 256-bit digest of a SHA-2 hash.
union Sha256  {
        uint8_t bytes[32];
        uint32_t words32[8];
};

// A SHA that is used as a placeholder. A warning should be issued if
// a null SHA is detected and the --ignore-hash flag is not present. An
// error must be thrown if a null sha collision is *generated*
#define NULL_SHA (union Sha256) { .bytes = {\
        0xe1u,0x6bu,0x57u,0xa5u,0x88u,0x03u,0xe4u,0x3cu,\
        0xbeu,0x51u,0x95u,0x8fu,0x62u,0x08u,0x27u,0x58u,\
        0xa1u,0xa5u,0x0du,0xa5u,0xceu,0xf7u,0xe3u,0x88u,\
        0x69u,0x4du,0xb8u,0xf1u,0x24u,0x3fu,0x1cu,0x40u } }

// Gets the number of zeros that would be appended to a message of `msgBytes` 
// octets.
uint64_t solveK(uint64_t msgBytes);

// Gets the length that a message of `msgBytes` octets would be padded to for 
// SHA-256 hashing. The return value is in bytes, not bits! If msgBytes is too large 
// (>= 2**61), this returns 0; this is the only error that can occur
uint64_t paddedLen(uint64_t msgBytes);

// Pads the message with a 1 bit, 0s, and the length of the message, until
// the message length is multiple of 512 bits. This follows the RFC spec 
// section 4.1. 
// 
// If the message is not successfully padded (e.g. the output buffer is too 
// small), this function will return 0; otherwise, it returns a non-zero value
// that is equal to the final padded message length, in bytes (not bits).
//
//  - msgBytes is too large (i.e. >= 2**61)
//  - msg is NULL
//  - outLen is too small to contain the padded output
//  - outBuf is NULL
//
// Note that outLen need not be a multiple of 64 (bytes) in order for the
// function to succeed; rather, any bytes beyond those necessary for the padding
// are ignored when
//
// outLen and msgBytes are measured in the number of bytes they store, not bits.
uint64_t padMsg(uint8_t *outBuf, uint64_t outBytes, const uint8_t *msg, 
        uint64_t msgBytes);

// Pads a partial block as though it is the last block of a message. This
// assumes that partialBlock comprises the last block of the message, and that 
// its length (the length of partialBlock) is equal to `msgBytes % 64`. Behavior 
// is undefined if it is not. The output buffer ought to be at least 128 bytes in 
// length in order to contain the padded block.
//
// This function returns the final length (in bytes) of the block after padding, 
// which is either 64 or 128 under normal circumstances, but is 0 if an error 
// occurs. All error states are identical to those for padMsg().
uint64_t padBlock(uint8_t *outBuf, uint64_t outBytes, 
        const uint8_t *partialBlock, uint64_t msgBytes);

// Shifts x right by n bits, and appends the n least significant bits to the
// left of x (most significant). Behavior is undefined if n is 32 or more
uint32_t rotr(uint32_t x, uint8_t n);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t ch(uint32_t x, uint32_t y, uint32_t z);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t maj(uint32_t x, uint32_t y, uint32_t z);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t bsig0(uint32_t x);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t bsig1(uint32_t x);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t ssig0(uint32_t x);

// Logical bitwise function as specified by RFC-6234 5.1
uint32_t ssig1(uint32_t x);

// Constants for SHA-256 as specified by RFC 6234 5.1
#define PRIME_CBRTS {\
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,\
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,\
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,\
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,\
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,\
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,\
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,\
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,\
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,\
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,\
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,\
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,\
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,\
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,\
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,\
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u }

// Sets all data of the hash to the 32-bit words found in RFC 6234 6.1.
// This returns 1 if no errors occur, and 0 otherwise. The only error
// that can occur is if dest is NULL.
uint8_t initSha256(union Sha256 *dest);

// Computes a complete SHA-256 hash of the 512-bit block, outputting the
// digest in big-endian format into the given destination pointer. All
// existing data in said destination will be overridden. The existing data 
// in the destination pointer will be used as the current value of the 
// hash. 
//
// This functions returns 1 if the computation was successful, and 0 if an
// error occurs. Any of the following can cause an error and return 0:
//
//  - block is NULL
//  - destination is NULL
//
// The block is not padded by this, and is computed as-is.
//
// This function is derived from the steps given in RFC 6234 section 6.2
uint8_t computeBlockHash(union Sha256 *destination, const uint8_t block[64]);

// Computes a complete SHA-256 hash of a message, outputting the digest 
// in big-endian format into the given destination pointer. All 
// existing data in said destination will be overridden; the value of 
// HASH_0 will be used as the initial hash, rather than any existing data
// in the destination.
//
// The provided message is internally padded before any hashing occurs;
// a consumer of this function need not (and should not) pad their message 
// beforehand.
//
// This function returns the number of blocks that were computed, or 0
// if an error occurred (even if something was computed). Any of the 
// following can cause an error and return 0:
//
//  - msg is NULL
//  - destination is NULL
//  - msgBytes is too large (>= 2**61)
//  - Memory could not be allocated for the padded message
// 
// If an error occurs, the data at destination is undefined.
//
// Please note that **msgBytes is measured in bytes.**, so a 256 bit 
// message would provide msgBytes as equaling 32.
//
// This function is derived from the steps given in RFC 6234 section 6.2
uint64_t computeHash(union Sha256 *destination, const uint8_t *msg, 
        uint64_t msgBytes);

// Returns 1 if and only if every byte of the two hashes is the same.
uint8_t hashesEqual(const union Sha256 *h1, const union Sha256 *h2);

// Writes the given file's SHA256 hash to the given output. This returns the 
// number of 512-bit blocks that were computed by this except when an error 
// occurs, in which it returns 0. This can only return 0 if an error occurs. 
// Messages are sent to stderr upon any error.
uint64_t computeFileHash(union Sha256 *dest, FILE *file);

#endif /* HASH_H */