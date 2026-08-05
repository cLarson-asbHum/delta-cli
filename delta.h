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

#include <stdio.h>
#include <stdint.h>

union SerialLong {
        uint64_t longVal;
        uint8_t bytes[8];
};

// Indicates that a part of the previous will be moved to the specified index.
// Moves can overlap with one another, which allows for deletions.
struct MoveCommand
{
        union SerialLong prevIndex; // Where the substring was in the previousSrc string
        union SerialLong curIndex;  // Where the substring is in the currentSrc string
        union SerialLong len;       // How long the substring
};

// Indicates that a specific symbol present in current was not present in
// previous.
struct AddCommand
{
        uint8_t symbol;
        union SerialLong curIndex; // Where the symbol is in the currentSrc string
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
                struct MoveCommand *move;
                struct AddCommand *add;
        } command;
};

// Frees all memory associated with the command recursively. This includes pointer 
// members (i.e. (struct Command).command). This returns the type member of the 
// command.
int freeCommand(struct Command *command);

// Computes the largest block move, as described by procedure L in Tichy (page 8).
// If either curMaxCount is 0, or prevStart is greater than or equal to prevLen,
// this returns NULL.
//
// previousSrc  - pointer to the first byte the previous src
// prevStart    - Index to start at in previous src
// prevLen      - length of previous src
// currentSrc   - pointer to the first byte of the prefix (start of L in Tichy)
// curMaxCount  - maximum length of the prefix
struct Command *nextLargestMove(const uint8_t *previousSrc,
                                uint64_t prevStart, uint64_t prevLen,
                                const uint8_t *currentSrc, 
                                uint64_t curMaxCount);

// Performs the operation specified by the command, outputing in the specified
// buffer. This returns the number of bytes written as a result of command. If
// the return value is not equal to `patchSizeOf(command)`, an error has occurred
// (most likely the end of a buffer, either input or output, was unexpectedly 
// reached).
//
// previousSrc - pointer to the first byte of the buffer onto which the patch 
//               command is applied
// prevLen - length of previous src, in number of chars
// out     - output destination of the command. Existing bytes in the 
//           command's domain will be overridden; all others are unmodified
// outLen  - length of out, in number of chars
// command - the patch command to apply and write to out
int patchCommand(const uint8_t *previousSrc, uint64_t prevLen,
                 uint8_t *out, uint64_t outLen, 
                 const struct Command *command);

// Determines how many bytes this command changes in the output. For adds, it's
// only one because only 1 symbol is added; for moves, it's equal to the `len` 
// struct member. If the type is garbage data, then this returns -1.
int patchSizeOf(const struct Command *command);

#define MAGIC_NUMBER ({ 'D', 'L', 'T', 'A'  })
#define VERSION_CHUNK_NAME ({ 'v', 'r', 's', 'n' })
#define CURRENT_VERSION 1
#define META_CHUNK_NAME ({ 'm', 'e', 't', 'a' })
#define DATA_CHUNK_NAME ({ 'd', 'a', 't', 'a' }) 

// Little-endian 256-bit digest of a SHA-2 hash
struct Sha256 {
        uint8_t bytes[32];
};

// All of the bytes that come before the actual data. This is a RIFF-like
// file. All multi-byte numbers (including those in the data) are 
// little-endian.
struct Version1Header {
        // Top-level info
        uint8_t magicNumber[4];  // MUST always be MAGIC_NUMBER
        uint8_t versionChunk[4]; // MUST always be VERSION_CHUNK_NAME
        uint8_t versionId;       // when serializing, always CURRENT_VERSION

        // Meta data
        uint8_t metaChunk[4];      // MUST always be META_CHUNK_NAME
        union SerialLong metaSize; // includes the size of the comment sub chunk
        uint8_t moveCommandSym;    // 'M' in version 1
        uint8_t addCommandSym;     // 'A' in version 1
        uint8_t cmdLensEqual;      // 1 iff serialSizeOf is equal for all CommandTypes
        struct Sha256 previousSrcHash; // for the file used to reconstruct
        struct Sha256 currentSrcHash;  // for the output of reconstruction
        union SerialLong outputSize;   // reconstructed file's length, in bytes
        uint8_t commentSubChunk[4];    // MUST always be COMMENT_CHUNK_NAME
        union SerialLong commentSize;  // in bytes
        uint8_t *comment; // length specified by commentSize

        // Actual data (commands)
        uint8_t dataChunkName;     // MUST always be DATA_CHUNK_NAME
        union SerialLong dataSize; // in bytes
        /* commands... */
};

// 0 - Little endian; 1 - big endian
uint8_t determineEndian();

// Determines how many bytes are necessary to serialize the given command. If 
// the type is garbage data, then this returns -1.
int serialSizeOf(const struct Command *command);

/* Writes the command as a series of bytes to the specified buffer, starting at
 * index i. This returns the number of bytes which were successfully written,
 * which is less than or equal to the serialization size of the given command.
 * If the number of bytes written is less than `serialSizeOf(command)`, an
 * error occurred.
 *
 * buf     - pointer to first uint8_t in the to-serialize output
 * bufSize - length of the buffer array, in number of uint8_ts
 * i       - the first index to write to when serializing the command
 * command - what to serialize
 */
int serializeCommand(uint8_t *buf, uint64_t bufSize,
                     uint64_t i, const struct Command *command);

/* Synthesizes a command from the given buffer, starting at the given index. This
 * is most useful when reading from a file. Parameter `destination` is used as
 * the result of deserialization.
 * 
 * This procedure assumes that the index `i` is, in fact, the first byte of a 
 * command; if it isn't (e.g. it's a part of a file's meta data), garbage data 
 * will be written to the command destination without returning any error 
 * (assuming no error described below occurs).
 *
 * When successful, the function returns `EXIT_SUCCESS` from stdio.h. If anything
 * else is returned, an error has occurred (e.g. end of the buffer was reached),
 * the destination pointer is in an undefined state.
 *
 * buf     - pointer to first uint8_t in the serialized source
 * bufSize - length of the buffer array, in number of uint8_ts
 * i       - the first index to read when deserializing the command
 * destination - where the deserialized data will be written
 */
int *deserializeCommand(const uint8_t *buf, uint64_t bufsize,
                        uint64_t i, struct Command *destination);

#endif