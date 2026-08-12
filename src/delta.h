// delta.h
//
// Computes and patches the smallest string-to-string correction using
// in terms of moves and additions.
//
// The algorithms for computing file deltas are derived from the following paper:
// Tichy, Walter F., "The String-to-String Correction Problem with Block Moves"
// (1983). Department of Computer Science Technical Reports. Paper 378.
// https://docs.lib.purdue.edu/cstech/378

#ifndef DELTA_H
#define DELTA_H

#include <stdint.h>
#include "hash.h"

union SerialLong {
        uint64_t longVal;
        uint8_t bytes[8];
};

// Indicates that a part of the source will be moved to the specified index.
// Moves can overlap with one another, which allows for deletions.
struct MoveCommand
{
        union SerialLong srcIndex; // Where the substring was in the source string
        union SerialLong tgtIndex; // Where the substring is in the target string
        union SerialLong len;      // How long the substring
};

// Indicates that a specific symbol present in target was not present in
// source.
struct AddCommand
{
        uint8_t symbol;
        union SerialLong tgtIndex; // Where the symbol is in the target string
};

enum CommandType
{
        MOVE_COMMAND = 'M',
        ADD_COMMAND  = 'A'
};

// A command of runtime-determined type.
struct Command
{
        enum CommandType type;
        union
        {
                struct MoveCommand move;
                struct AddCommand add;
        } cmd;
};

// Computes the largest block move, as described by procedure L in Tichy (page 8).
// If either tgtMaxCount is 0, or srcStart is greater than or equal to srcLen,
// this returns NULL.
//
// source   - pointer to the first byte of the source string
// srcStart - Index to start at in the source string
// srcLen   - length of the source string
// target   - pointer to the first byte of the prefix (start of L in Tichy)
// tgtMaxCount - maximum length of the prefix
struct Command *nextLargestMove(const uint8_t *source, uint64_t srcStart, 
        uint64_t srcLen, const uint8_t *target, uint64_t tgtMaxCount);

#define GARBAGE_PATCH_SIZE UINT64_MAX

// Performs the operation specified by the command, outputing in the specified
// buffer. This returns the number of bytes written as a result of command. If
// the return value is not equal to `patchSizeOf(command)`, an error has occurred
// (most likely the end of a buffer, source or output, was unexpectedly 
// reached).
//
// If the command type is not recognized, this returns 0, which is conveniently 
// not equal to `patchSizeOf()` for unrecognized commands.
//
// source  - pointer to the first byte of the buffer onto which the patch 
//               command is applied
// srcLen  - length of the source string
// out     - output destination of the command. Existing bytes in the 
//           command's domain will be overridden; all others are unmodified
// outLen  - length of out, in number of chars
// command - the patch command to apply and write to out
uint64_t patchCommand(const uint8_t *source, uint64_t srcLen,
                 uint8_t *out, uint64_t outLen, 
                 const struct Command *command);

// Determines how many bytes this command changes in the output. For adds, it's
// only one because only 1 symbol is added; for moves, it's equal to the `len` 
// struct member. If the type is garbage data, then this returns
// GARBAGE_PATCH_SIZE
uint64_t patchSizeOf(const struct Command *command);

#define MAGIC_NUMBER { 'D', 'L', 'T', 'A'  }
#define VERSION_CHUNK_NAME { 'v', 'r', 's' }
#define CURRENT_VERSION 1
#define META_CHUNK_NAME { 'm', 'e', 't', 'a' }
#define DATA_CHUNK_NAME { 'd', 'a', 't', 'a' } 

#define V1_META_PADDING (3)
// Value for the Version1Header metaSize member. 
#define V1_META_SIZE (32 + 32 + 1 + V1_META_PADDING)

// Size of the header for version 1 files.
#define V1_DELTA_HEADER_SIZE (4 + 3 + 1 + 8 + 4 + 8 + V1_META_SIZE + 4 + 8)

// All of the bytes that come before the actual data. This is a RIFF-like
// file. All multi-byte numbers (including those in the data) are 
// little-endian.
struct Version1Header {
        // Meta data
        uint8_t metaChunk[4];    // MUST always be META_CHUNK_NAME
        union SerialLong metaSize;
        union Sha256 sourceHash; // for the file used to reconstruct
        union Sha256 targetHash; // for the output of reconstruction
        uint8_t cmdLensEqual;    // 1 iff serialSizeOf is equal for all CommandTypes
        
        // Padding which aligns the meta chunk size to 16 bytes:
        uint8_t paddingNoOneShouldEverReadOrWriteTo[V1_META_PADDING];
};

struct DeltaHeader {
        // Top-level info
        uint8_t magicNumber[4];     // MUST always be MAGIC_NUMBER
        uint8_t versPseudoChunk[3]; // MUST always be VERSION_CHUNK_NAME
        uint8_t versionId;
        union SerialLong targetSize; // reconstructed file's length, in bytes

        /* ... other chunks (if any) ... */

        // Version-specific chunks
        union {
                struct Version1Header v1;
        } header;

        /* ... other chunks (if any) ... */

        // Actual data (commands)
        uint8_t dataChunkName[4];  // MUST always be DATA_CHUNK_NAME
        union SerialLong dataSize; // in bytes
        /* commands... */
};

// 0 - Little endian; 1 - big endian
uint8_t determineEndian();

// Serializes a little-endian formatted long into a buffer from a SerialLong
// which stores the bytes formatted in the system's endianess
uint8_t littleSerialize(uint8_t *buf, uint64_t bufSize, uint64_t start, 
        const union SerialLong *num);
        
// Deserializes a little-endian formatted long from a buffer into a SerialLong
// which will store the bytes formatted in the system's endianess
uint8_t littleDeserialize(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
        union SerialLong *out);

#define GARBAGE_SERIAL_SIZE UINT8_MAX

// ||Type byte|| + ||symbol|| + ||tgtIndex||, 
#define ADD_SERIAL_SIZE (uint8_t) (1 + 1 + 8)

// ||Type byte|| + ||srcIndex|| + ||tgtIndex|| + ||len||
#define MOVE_SERIAL_SIZE (uint8_t) (1 + 8 + 8 + 8)


// Determines how many bytes are necessary to serialize the given command. If 
// the type is garbage data, then this returns GARBAGE_SERIAL_SIZE.
uint8_t serialSizeOf(const struct Command *command);

/* Writes the command as a series of bytes to the specified buffer, starting at
 * index i. This returns the number of bytes which were successfully written,
 * which is less than or equal to the serialization size of the given command.
 * If the number of bytes written does not equal `serialSizeOf(command)`, an
 * error occurred. 
 * 
 * If the command type is not recognized, this returns 0, which is conveniently 
 * not equal to `serialSizeOf()` for garbage commands.
 *
 * buf     - pointer to first uint8_t in the to-serialize output
 * bufSize - length of the buffer array, in number of uint8_ts
 * i       - the first index to write to when serializing the command
 * command - what to serialize
 */
uint8_t serializeCommand(uint8_t *buf, uint64_t bufSize,
                     uint64_t i, const struct Command *command);

/* Synthesizes a command from the given buffer, starting at the given index. This
 * is most useful when reading from a file. Parameter `destination` is used as
 * the result of deserialization.
 * 
 * This procedure assumes that the index `i` is, in fact, the first byte of a 
 * command; if it isn't (e.g. it's a part of a file's meta data), garbage data 
 * will be written to the command destination, with or without error.
 *
 * This function returns the number of bytes read and deserialized. If this 
 * isn't equal to `serialSizeOf(destination)`, then an error has occurred. If
 * the read command type (the first byte deserialized) is not recognized, this
 * returns 1, which is conveniently not equal to `serialSizeOf()` for garbage 
 * commands.
 *
 * buf     - pointer to first uint8_t in the serialized source
 * bufSize - length of the buffer array, in number of uint8_ts
 * i       - the first index to read when deserializing the command
 * destination - where the deserialized data will be written
 */
uint8_t deserializeCommand(const uint8_t *buf, uint64_t bufSize,
                        uint64_t i, struct Command *destination);

#endif