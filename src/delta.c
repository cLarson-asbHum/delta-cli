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
        int p = srcStart; // Index in source
        int l = 0;         // Length of the move (0 if addition)

        const int pStart = srcStart;
        int pCur = srcStart;

        // Checking over every character in source, starting at pStart, 
        // wrapping around if we hit the end of source, and ending when 
        // the next pCur is at pStart
        while ((pCur + 1) % srcLen != pStart && tgtMaxCount > l) {
                // Determine length of match between source[pCur....] 
                // and target[q,...]
                int lCur = 0;
                int inBounds = (pCur + lCur < srcLen) && (lCur < tgtMaxCount);

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

int patchSizeOf(const struct Command *command) {
        int commandType = command->type; // Might be garbage
        switch (commandType) {
        case ADD_COMMAND:
                return 1;

        case MOVE_COMMAND:
                return command->cmd.move.len.longVal;
        
        default:
                // We were provided garbage data
                return -1;
        }
}

int serialSizeOf(const struct Command *command) {
        int commandType = command->type; // Might be garbage
        switch (commandType) {
        case ADD_COMMAND:
                return ADD_SERIAL_SIZE;

        case MOVE_COMMAND:
                return MOVE_SERIAL_SIZE;
        
        default:
                // We were provided garbage data
                return -1;
        }
}

int patchMove(const uint8_t *source, uint64_t srcLen, uint8_t *out, 
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

int patchAdd(uint8_t *out, uint64_t outLen, const struct AddCommand *add) 
{
        if(add->tgtIndex.longVal >= outLen) {
                return 0;
        }

        out[add->tgtIndex.longVal] = add->symbol;
        return 1;
}

int patchCommand(const uint8_t *source, uint64_t srcLen, uint8_t *out, 
        uint64_t outLen, const struct Command *command) 
{
        switch (command->type) {
        case MOVE_COMMAND:
                return patchMove(source, srcLen, out, outLen, 
                        &(command->cmd.move));
        
        case ADD_COMMAND:
                return patchAdd(out, outLen, &(command->cmd.add));

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
int littleSerialize(uint8_t *buf, uint64_t bufSize, uint64_t start, 
        const union SerialLong *num) 
{
        if(start + 8 > bufSize) {
                return 0;
        }

        if(determineEndian() == 0) {
                // Little endian
                for(int i = 0; i < 8; i++) {
                        buf[i + start] = num->bytes[i];
                }
        } else {
                // Big endian
                for(int i = 0; i < 8; i++) {
                        buf[i + start] = num->bytes[7 - i];
                }
        }

        return 8;
}

int serializeAdd(uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     const struct AddCommand *add) 
{
        if(start + ADD_SERIAL_SIZE > bufSize) {
                return 0;
        }

        buf[start] = (uint8_t) ADD_COMMAND;
        buf[start + 1] = add->symbol;
        const int read = littleSerialize(buf, bufSize, start + 2, 
                &(add->tgtIndex));
        return 2 + read; // Assuming all went right, this equals ADD_SERIAL_SIZE
}

int serializeMove(uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     const struct MoveCommand *move) 
{
        if(start + MOVE_SERIAL_SIZE > bufSize) {
                return 0;
        }

        uint64_t i = start;
        buf[i] = (uint8_t) MOVE_COMMAND;
        i++;

        // Serializing the source index
        const int srcIndexRet = littleSerialize(buf, bufSize, i, &(move->srcIndex));
        if(srcIndexRet != 8) {
                return i + srcIndexRet;
        }
        i += 8;
                
        // Serializing the target index
        const int tgtIndexRet = littleSerialize(buf, bufSize, i, &(move->tgtIndex));
        if(tgtIndexRet != 8) {
                return i + tgtIndexRet;
        }
        i += 8;
                
        // Serializing the length
        const int lenRet = littleSerialize(buf, bufSize, i, &(move->len));
        return i - start + lenRet; // Assuming all went right, this equals MOVE_SERIAL_SIZE
}

int serializeCommand(uint8_t *buf, uint64_t bufSize, uint64_t i, 
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
                
        default:
                return 0;
        }
}

// Deserializes a little-endian formatted long from a buffer into a SerialLong
// respecting the system's endianness.
int littleDeserialize(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
        union SerialLong *out) 
{
        if(start + 8 > bufSize) {
                return 0;
        }

        if(determineEndian() == 0) {
                // Little endian
                for(int i = 0; i < 8; i++) {
                        out->bytes[i] = buf[start + i];
                }
        } else {
                // Big endian
                for(int i = 0; i < 8; i++) {
                        out->bytes[7 - i] = buf[start + i];
                }
        }

        return 8;
}

int deserializeMove(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     struct MoveCommand *move)
{
        if(start + MOVE_SERIAL_SIZE - 1 > bufSize) {
                return 0;
        }

        uint64_t i = start;

        // Serializing the source index
        const int srcIndexRet = littleDeserialize(buf, bufSize, i, &(move->srcIndex));
        if(srcIndexRet != 8) {
                return i + srcIndexRet;
        }
        i += 8;
                
        // Serializing the target index
        const int tgtIndexRet = littleDeserialize(buf, bufSize, i, 
                &(move->tgtIndex));
        if(tgtIndexRet != 8) {
                return i + tgtIndexRet;
        }
        i += 8;
                
        // Serializing the length
        const int lenRet = littleDeserialize(buf, bufSize, i, &(move->len));
        return i - start + lenRet; // Assuming all went right, this equals MOVE_SERIAL_SIZE
}

int deserializeAdd(const uint8_t *buf, uint64_t bufSize, uint64_t start, 
                     struct AddCommand *add)
{
        if(start + ADD_SERIAL_SIZE - 1 > bufSize) {
                return 0;
        }

        add->symbol = buf[start];
        const int read = littleDeserialize(buf, bufSize, start + 1, &(add->tgtIndex));
        return 1 + read; // This should always equal ADD_SERIAL_SIZE
}


int deserializeCommand(const uint8_t *buf, uint64_t bufSize, uint64_t i, 
                     struct Command *dest) 
{
        if(i >= bufSize) {
                return 0;
        }

        const enum CommandType type = buf[i];
        dest->type = type;
        switch(type) {
        case MOVE_COMMAND:
                return 1 + deserializeMove(buf, bufSize, i + 1, &(dest->cmd.move));
        case ADD_COMMAND:
                return 1 + deserializeAdd(buf, bufSize, i + 1, &(dest->cmd.add));
        default: 
                return 1;
        }
}
