#include "delta.h"

#include <stdlib.h>

struct Command *nextLargestMove(const uint8_t *source,
                                uint64_t srcStart, uint64_t srcLen,
                                const uint8_t *target, 
                                uint64_t tgtMaxCount) 
{
        if(srcStart >= srcLen || tgtMaxCount <= 0) {
                return NULL;
        }

        // Finding the maximal l and its corresponding p
        uint64_t p = srcStart; // Index in source
        uint64_t l = 0;        // Length of the move (0 if addition)

        const uint64_t pStart = srcStart;
        uint64_t pCur = srcStart;

        // Checking over every character in source, starting at pStart, 
        // wrapping around if we hit the end of source, and ending when 
        // the next pCur is at pStart
        while ((pCur + 1) % srcLen != pStart && tgtMaxCount > l) {
                // Determine length of match between source[pCur....] 
                // and target[q,...]
                uint64_t lCur = 0;
                uint64_t inBounds = (pCur + lCur < srcLen) && (lCur < tgtMaxCount);

                // TODO: Request more characters
                while (inBounds && source[pCur + lCur] == target[lCur]) {
                        lCur++;
                        inBounds = (pCur + lCur < srcLen) && (lCur < tgtMaxCount);
                }

                if (lCur > l) {
                        // New maximum found
                        l = lCur;
                        p = pCur;
                }

                // Wrapping back to the start if we reach the end of source
                pCur = (pCur + 1) % srcLen;
        }

        // Formatting our result as a command
        // NOTE: The return of this method MUST be freed.
        struct Command *result = malloc(sizeof(struct Command));

        if(l == 0) {
                result->type = ADD_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.add.tgtIndex.longVal = 0;
                result->cmd.add.symbol = target[0];
        } else {
                result->type = MOVE_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.move.tgtIndex.longVal = 0;
                result->cmd.move.srcIndex.longVal = p;
                result->cmd.move.len.longVal = l;
        }

        return result;
}

// Reads the address of X, and dereferences it as a uint64_t
#define READ_64(X) ( *((uint64_t *) &(X)) )

struct Command *nextLargest64Move(const uint64_t *source, uint64_t srcLen, 
        const uint64_t *target, uint64_t tgtMaxCount) 
{
        if(srcLen <= 0 || srcLen > (UINT64_MAX / 8) || tgtMaxCount <= 0) {
                return NULL;
        }

        const uint8_t *src8b = (uint8_t *) source;
        const uint64_t src8bLen = srcLen * 8;
        
        // Finding the maximal l and its corresponding p
        uint64_t p = 0; // Index to a byte, not a uint64_t, in src8b
        uint64_t l = 0; // Length of the move (0 if addition), in bytes (not uints)
        uint64_t pCur = 0; // Index to a bytes, not a uint64

        // Checking over every character in source, starting at pStart, 
        // wrapping around if we hit the end of source, and ending when 
        // the next pCur is at pStart
        while (pCur < src8bLen && l < tgtMaxCount) {
                // Determine length of match between source[pCur....] 
                // and target[q,...]
                uint64_t lCur = 0; // Number of bytes
                uint64_t inBounds = (pCur + lCur < src8bLen) && (lCur < tgtMaxCount);

                while (inBounds && READ_64(src8b[pCur + lCur]) == target[lCur / 8]) {
                        lCur += 8;
                        inBounds = (pCur + lCur < src8bLen) && (lCur < tgtMaxCount);
                }

                if (lCur > l) {
                        // New maximum found
                        l = lCur;
                        p = pCur;
                }

                pCur++;
        }

        // Formatting our result as a command
        // NOTE: The return of this method MUST be freed.
        struct Command *result = malloc(sizeof(struct Command));

        if(l == 0) {
                result->type = ADD_64_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.add64.tgtIndex.longVal = 0;
                result->cmd.add64.symbol64 = target[0];
        } else {
                result->type = MOVE_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.move.tgtIndex.longVal = 0;
                result->cmd.move.srcIndex.longVal = p;
                result->cmd.move.len.longVal = l;
        }

        return result;
}

uint64_t readUint(const struct UintNArray *arr, uint64_t i) 
{
        const uint8_t w = arr->byteWidth;
        uint64_t ret = 0;
        for (int j = 0; j < w; j++) {
                ret |= arr->bytes[w * i + j] << (8 * j);
        }
        return ret;
}

// Performs a binary search for the specified element, using an array of indexes 
// into another array. If the element is not found, the index where it *would* 
// be is returned. If the element is repeated within the array, the index to 
// the first repetition is returned. The output range is [0, arrLen].
//
// Overflow of the search index is not properly handled, and as such care should 
// be made that the length of the array is less than `UINT64_MAX / 2`.
uint64_t searchSorted(const uint64_t *arr, const struct UintNArray *indices, 
        uint64_t arrLen, uint64_t elem) 
{
        uint64_t left = 0; // Inclusive
        uint64_t right = arrLen; // Exclusive
        uint64_t mid; 
        uint64_t cur;
        
        while (left < right) {
                mid = (right - 1 + left) / 2; // Please don't overflow!!1!
                cur = arr[readUint(indices, mid)];

                if (cur >= elem) {
                        // We overshot the target element
                        // We include the equality case, as we want the left-most 
                        // repetition of elem (if there are repetitions)
                        right = mid;
                } else {
                        // We undershot the target element
                        left = mid + 1;
                }
        }

        // The two bounds are equal by this point
        return left;
}

// Computes the largest block move under the assumption that the indices are 
// sorted by ascending value of the source symbols. For example, suppose the 
// symbols (as values) in source are `{ 1, 5, 4, 3 }`; the srcIndices for this
// source would be `{ 0, 3, 2, 1 }` because source[0] < source[3] < source[2] 
// < source[1]. 
//
// If srcIndices is NULL or its byteWidth member is not between 1 and 8 
// (inclusive), this returns NULL.
//
// This does not validate that srcIndices are actually sorted nor that they 
// point to elements in source; both are assumed, and behavior is undefined 
// otherwise.
//
// Unless noted above, this behaves identical to nextLargest64Move() in regards
// to parameters, error states, return values.
struct Command *nextLargest64Sorted(const uint64_t *source, 
        const struct UintNArray *srcIndices, uint64_t srcLen, 
        const uint64_t *target, uint64_t tgtMaxCount) 
{
        if(srcLen <= 0 || srcLen > (UINT64_MAX / 8) || tgtMaxCount <= 0) {
                return NULL;
        }
        
        // Finding the maximal l and its corresponding p
        uint64_t p = 0; // Index to a uint64_t, not a byte, in src
        uint64_t l = 0; // Length of the move (0 if addition), in uints (not bytes)

        // Searching in source for the target prefix
        // Note that we limit srcLen to `UINT8_MAX / 8` uint64s (2.00 Exbibytes)
        uint64_t sortedI = searchSorted(source, srcIndices, srcLen, target[0]);
        uint64_t pCur = readUint(srcIndices, sortedI);

        // Iterating over each duplicate of our prefix
        while(pCur < srcLen && l < tgtMaxCount && source[pCur] == target[0]) {
                // Getting the largest shared substring we can starting at pCur
                uint64_t lCur = 1; // At least one (outer loop would've exited otherwise)
                while (pCur + lCur < srcLen && lCur < tgtMaxCount
                        && source[pCur + lCur] == target[lCur])
                {
                        lCur++;
                }

                if (lCur > l) {
                        // New maximum found
                        l = lCur;
                        p = pCur;
                }

                // Going to the next sorted repetition.
                sortedI++;
                pCur = readUint(srcIndices, sortedI);
        }

        // Formatting our result as a command
        // NOTE: The return of this method MUST be freed.
        struct Command *result = malloc(sizeof(struct Command));
        if(l == 0) {
                result->type = ADD_64_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.add64.tgtIndex.longVal = 0;
                result->cmd.add64.symbol64 = target[0];
        } else {
                result->type = MOVE_COMMAND;
                
                // NOTE: Consumers MUST override the value of tgtIndex
                result->cmd.move.tgtIndex.longVal = 0;
                result->cmd.move.srcIndex.longVal = 8 * p;
                result->cmd.move.len.longVal = 8 * l;
        }

        return result;
}

uint64_t patchSizeOf(const struct Command *command) {
        uint8_t commandType = command->type; // Might be garbage
        switch (commandType) {
        case ADD_COMMAND:
                return 1uLL;

        case MOVE_COMMAND:
                return command->cmd.move.len.longVal;

        case ADD_64_COMMAND:
                return 8uLL;
        
        default:
                // We were provided garbage data
                return GARBAGE_PATCH_SIZE;
        }
}

uint64_t lastPatchedIndex(const struct Command *command) 
{
        switch (command->type) {
        case ADD_COMMAND:
                return command->cmd.add.tgtIndex.longVal;
        case MOVE_COMMAND:
                const struct MoveCommand *move = &command->cmd.move;
                return move->tgtIndex.longVal + move->len.longVal;
        case ADD_64_COMMAND:
                return command->cmd.add.tgtIndex.longVal + 8uLL;
        default:
                GARBAGE_PATCH_SIZE;
        }
}

uint8_t minVersion(enum CommandType type) {
        switch (type) {
        case ADD_COMMAND:
        case MOVE_COMMAND:
                return 1;

        case ADD_64_COMMAND:
                return 2;

        default: 
                return GARBAGE_VERSION;
        }
}

uint8_t serialSizeOf(const struct Command *command) {
        uint8_t commandType = command->type; // Might be garbage
        switch (commandType) {
        case ADD_COMMAND:
                return ADD_SERIAL_SIZE;

        case MOVE_COMMAND:
                return MOVE_SERIAL_SIZE;

        case ADD_64_COMMAND:
                return ADD_64_SERIAL_SIZE;
        
        default:
                // We were provided garbage data
                return GARBAGE_SERIAL_SIZE;
        }
}

uint64_t patchMove(const uint8_t *source, uint64_t srcLen, uint8_t *out, 
              uint64_t outLen, const struct MoveCommand *move) 
{
        const uint64_t srcI = move->srcIndex.longVal;
        const uint64_t outI = move->tgtIndex.longVal;
        const uint64_t len  = move->len.longVal;
        uint64_t i = 0;

        while((i < len) && (srcI + i < srcLen) && (outI + i < outLen)) {
                out[outI + i] = source[srcI + i];
                i++;
        }

        return i;
}

uint64_t patchAdd(uint8_t *out, uint64_t outLen, const struct AddCommand *add) 
{
        if(add->tgtIndex.longVal >= outLen) {
                return 0;
        }

        out[add->tgtIndex.longVal] = add->symbol;
        return 1uLL;
}

uint64_t patchAdd64(uint8_t *out, uint64_t outLen, const struct Add64Command *add) 
{
        if(add->tgtIndex.longVal + 8 > outLen) {
                return 0;
        }

        ((uint64_t *) &out[add->tgtIndex.longVal])[0] = add->symbol64;
        return 8uLL;
}

uint64_t patchCommand(const uint8_t *source, uint64_t srcLen, uint8_t *out, 
        uint64_t outLen, const struct Command *command) 
{
        switch (command->type) {
        case MOVE_COMMAND:
                return patchMove(source, srcLen, out, outLen, 
                        &(command->cmd.move));
        
        case ADD_COMMAND:
                return patchAdd(out, outLen, &(command->cmd.add));

        case ADD_64_COMMAND:
                return patchAdd64(out, outLen, &(command->cmd.add64));

        default:
                return 0;
        }
}

uint8_t determineEndian() {
        union IntBytes {
                uint32_t i;
                uint8_t b[4];
        } val;

        val.i = 0x04030201u;

        if((val.b[0] == (uint8_t) 1) && (val.b[1] == (uint8_t) 2)) {
                return 0; // Little endian
        }

        // Should be 1 for big endian, but just in case...
        return val.b[3];
}

// Serializes a little-endian formatted long from a buffer into a SerialLong
// respecting the system's endianness.
uint8_t littleSerialize(uint8_t *buf, uint64_t bufSize, uint64_t start, 
        const union SerialLong *num) 
{
        if(start + 8 > bufSize) {
                return 0;
        }

        if(determineEndian() == 0) {
                // Little endian
                for(uint32_t i = 0; i < 8; i++) {
                        buf[i + start] = num->bytes[i];
                }
        } else {
                // Big endian
                for(uint32_t i = 0; i < 8; i++) {
                        buf[i + start] = num->bytes[7 - i];
                }
        }

        return 8;
}

uint8_t serializeAdd(uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     const struct AddCommand *add) 
{
        if(start + ADD_SERIAL_SIZE > bufSize) {
                return 0;
        }

        buf[start] = (uint8_t) ADD_COMMAND;
        buf[start + 1] = add->symbol;
        const uint8_t read = littleSerialize(buf, bufSize, start + 2, 
                &(add->tgtIndex));
        return 2 + read; // Assuming all went right, this equals ADD_SERIAL_SIZE
}

uint8_t serializeMove(uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     const struct MoveCommand *move) 
{
        if(start + MOVE_SERIAL_SIZE > bufSize) {
                return 0;
        }

        uint64_t i = start;
        buf[i] = (uint8_t) MOVE_COMMAND;
        i++;

        // Serializing the source index
        const uint8_t srcIndexRet = littleSerialize(buf, bufSize, i, 
                &(move->srcIndex));
        if(srcIndexRet != 8) {
                return i + srcIndexRet;
        }
        i += 8;
                
        // Serializing the target index
        const uint8_t tgtIndexRet = littleSerialize(buf, bufSize, i, 
                &(move->tgtIndex));
        if(tgtIndexRet != 8) {
                return i + tgtIndexRet;
        }
        i += 8;
                
        // Serializing the length
        const uint8_t lenRet = littleSerialize(buf, bufSize, i, &(move->len));
        return i - start + lenRet; // Assuming all went right, this equals MOVE_SERIAL_SIZE
}

uint8_t serializeAdd64(uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     const struct Add64Command *add64) 
{
        if(start + ADD_64_SERIAL_SIZE > bufSize) {
                return 0;
        }

        uint8_t read = 0;
        buf[start] = (uint8_t) ADD_64_COMMAND;
        read++;
        ((uint64_t *) &buf[start + read])[0] = add64->symbol64;
        read += 8;

        // Assuming all went right, this equals ADD_64_SERIAL_SIZE:
        return read + littleSerialize(buf, bufSize, start + read, &add64->tgtIndex);
}

uint8_t serializeCommand(uint8_t *buf, uint64_t bufSize, uint64_t i, 
                     const struct Command *command) 
{
        if(i >= bufSize) {
                return 0;
        }

        switch(command->type) {
        case ADD_COMMAND:
                return serializeAdd(buf, bufSize, i, &(command->cmd.add));
                
        case MOVE_COMMAND:
                return serializeMove(buf, bufSize, i, &(command->cmd.move));
                
        case ADD_64_COMMAND:
                return serializeAdd64(buf, bufSize, i, &(command->cmd.add64));

        default:
                return 0;
        }
}

// Deserializes a little-endian formatted long from a buffer into a SerialLong
// respecting the system's endianness.
uint8_t littleDeserialize(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
        union SerialLong *out) 
{
        if(start + 8 > bufSize) {
                return 0;
        }

        if(determineEndian() == 0) {
                // Little endian
                for(uint8_t i = 0; i < 8; i++) {
                        out->bytes[i] = buf[start + i];
                }
        } else {
                // Big endian
                for(uint8_t i = 0; i < 8; i++) {
                        out->bytes[7 - i] = buf[start + i];
                }
        }

        return 8;
}

uint8_t deserializeMove(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     struct MoveCommand *move)
{
        if(start - 1uLL + MOVE_SERIAL_SIZE > bufSize) {
                return 0;
        }

        uint64_t i = start;

        // Serializing the source index
        const uint8_t srcIndexRet = littleDeserialize(buf, bufSize, i, 
                &(move->srcIndex));
        if(srcIndexRet != 8) {
                return i + srcIndexRet;
        }
        i += 8;
                
        // Serializing the target index
        const uint8_t tgtIndexRet = littleDeserialize(buf, bufSize, i, 
                &(move->tgtIndex));
        if(tgtIndexRet != 8) {
                return i + tgtIndexRet;
        }
        i += 8;
                
        // Serializing the length
        const uint8_t lenRet = littleDeserialize(buf, bufSize, i, 
                &(move->len));
        return lenRet + (uint8_t) (i - start); // Assuming all went right, this equals MOVE_SERIAL_SIZE
}

uint8_t deserializeAdd(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     struct AddCommand *add)
{
        if(start - 1uLL + ADD_SERIAL_SIZE > bufSize) {
                return 0;
        }

        add->symbol = buf[start];
        const uint8_t read = littleDeserialize(buf, bufSize, start + 1, 
                &(add->tgtIndex));
        return 1 + read; // This should always equal ADD_SERIAL_SIZE
}

uint8_t deserializeAdd64(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     struct Add64Command *add64)
{
        if(start - 1uLL + ADD_64_SERIAL_SIZE > bufSize) {
                return 0;
        }

        add64->symbol64 = ((uint64_t *) &buf[start])[0];

        // This should always equal ADD_64_SERIAL_SIZE:
        return 8 + littleDeserialize(buf, bufSize, start + 8, &add64->tgtIndex);
}


uint8_t deserializeCommand(const uint8_t *buf, uint64_t bufSize, uint64_t i, 
                     struct Command *dest) 
{
        if(i >= bufSize) {
                return 0;
        }

        const enum CommandType type = buf[i];
        dest->type = type;
        switch(type) {
        case MOVE_COMMAND:
                return 1u + deserializeMove(buf, bufSize, i + 1, 
                        &(dest->cmd.move));
        case ADD_COMMAND:
                return 1u + deserializeAdd(buf, bufSize, i + 1, 
                        &(dest->cmd.add));
        case ADD_64_COMMAND:
                return 1u + deserializeAdd64(buf, bufSize, i + 1, 
                        &(dest->cmd.add64));
        default: 
                return 1u;
        }
}
