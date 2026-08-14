// hash.h
//
// Methods for creating, grabbing, and verifying hashes

#ifndef HASH_H
#define HASH_H

#include <stdint.h>
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
// msg and outBuf will be stored in little endian format. 
uint64_t padMsg(uint8_t *outBuf, uint64_t outBytes, const uint8_t *msg, 
        uint64_t msgBytes);

#endif 