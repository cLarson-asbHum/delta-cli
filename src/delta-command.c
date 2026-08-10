#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "delta-command.h"
#include "delta.h"
#include "file-helper.h"

struct LinkedCommand {
        struct Command *elem;
        struct LinkedCommand *next;
};

int freeLinked(struct LinkedCommand *head) {
        struct LinkedCommand *cur = head;
        int i = 0;
        while (cur != NULL) {
                struct LinkedCommand *prev = cur;
                cur = cur->next;
                free(prev->elem);
                free(prev);
                i++;
        }
        return i;
}

void verboseCmdLog(const struct Command *command) 
{
        if (command->type == ADD_COMMAND && (getLogFlags() & VERBOSE_FLAG)) {
                const char c = (char) (command->cmd.add.symbol);
                const uint8_t qSet = command->cmd.add.tgtIndex.longVal;
                verbose("   \\___ Command: ADD '%c' at %d \n", c, qSet);
                return ;
        }

        if (command->type == MOVE_COMMAND && (getLogFlags() & VERBOSE_FLAG)) {
                const uint64_t pSet = command->cmd.move.srcIndex.longVal;
                const uint64_t qSet = command->cmd.move.tgtIndex.longVal;
                const uint64_t l    = command->cmd.move.len.longVal;
                verbose("   \\___ Command: MOVE %d -> %d (length %d) \n", pSet, 
                        qSet, l);
                return ;
        }

        // Garbage data
        verbose("   \\___ Garbage command (type <%d>)\n", command->type);
}

void debugLinked(struct LinkedCommand *head) 
{
        if (DEBUG) {
                struct LinkedCommand *cur = head;
                int i = 0;
                while (cur != NULL) {
                        if (cur->elem != NULL) {
                                debug("'%c'", cur->elem->type);
                        } else {
                                debug("null");
                        }
                        if (cur->next != NULL) {
                                debug(" --> ");
                        }
                        cur = cur->next;
                        i++;
                }
                printf(" (%d)\n", i);
        }
}

uint64_t computeCmds(const struct FileBin *s, const struct FileBin *t, 
        struct LinkedCommand *head) 
{
        struct LinkedCommand *last = head;
        uint64_t q = 0;
        uint64_t outSize = 0;

        while (q < t->size) {
                normal(" \\___ %d / %d (%.2f%%)\n", q, t->size, 
                        100.0f * (float) q / (float) t->size);

                // TODO: Start from the last p.
                struct Command *command = nextLargestMove(s->buf, 0, s->size, 
                        &(t->buf[q]), t->size - q);

                if (command->type == ADD_COMMAND) {
                        command->cmd.add.tgtIndex.longVal = q;
                } else {
                        command->cmd.move.tgtIndex.longVal = q;
                }
                
                verboseCmdLog(command);
                q += patchSizeOf(command);
                outSize += serialSizeOf(command);

                // Appending our command to the end of a singly-linked list.
                // We add to the last elem rather than the head in order to
                // have first-in-first-out iteration.
                struct LinkedCommand *append = malloc(sizeof(struct LinkedCommand));
                append->elem = command;
                append->next = NULL;
                last->next = append; // Appending our node onto the last node
                last = append; // Making our node the last node.
        }

        return outSize;
}

// Returns 0 if an error occurred
uint64_t computeCmdsFromArgs(const struct Slurped *args, struct LinkedCommand *head) 
{
        // Reading the contents of our src files into buffers
        struct FileBin *s = readBin(args->posArg1, args->posArg1Len);
        if (s == NULL) {
                return 0;
        }
        
        struct FileBin *t = readBin(args->posArg2, args->posArg2Len);
        if (t == NULL) {
                freeBin(s);
                return 0;
        }

        // Computing the commands
        normal("Computing ... (takes a lot of time)\n");
        const uint64_t outSize = computeCmds(s, t, head);
        debugLinked(head);
        freeBin(s);
        freeBin(t);
        return outSize;
}

// Returns EXIT_SUCCESS if successful; EXIT_FAILURE otherwise. Error messages are
// printed on error
uint32_t outputHeaderV1(FILE *outFile, const struct Version1Header *header)
{
        const uint64_t outSize = V1_HEADER_RAW_SIZE;
        debug("Allocating %d bytes for header\n", outSize);
        uint8_t *outBuf = malloc(outSize);

        if (outBuf == NULL) {
                error("Error while serializing header: Could not allocate %d bytes\n", 
                        outSize);
                return EXIT_FAILURE;
        }

        // Serializing the header
        // WARNING: We assume all the header data is initialized and outBuf is large enough
        verbose("Serializing v1 header\n");
        debug("Serializing the top level data (index = 0)\n");
        uint32_t i = 0;
        memcpy(&outBuf[i], header->magicNumber,  4);
        i += 4;
        memcpy(&outBuf[i], header->versPseudoChunk, 3);
        i += 3;
        outBuf[i] = header->versionId;
        i += 1;

        debug("Serializing the meta chunk (index = %d)\n", i);
        memcpy(&outBuf[i],  header->metaChunk, 4);
        i += 4;
        littleSerialize(&outBuf[i], 8, 0, &header->metaSize);
        i += 8;
        memcpy(&outBuf[i], header->sourceHash.bytes, 32);
        i += 32;
        memcpy(&outBuf[i], header->targetHash.bytes, 32);
        i += 32;
        littleSerialize(&outBuf[i], 8, 0, &header->targetSize);
        i += 8;
        outBuf[i] = header->cmdLensEqual;
        i += 1;
        outBuf[i] = header->moveCommandSym;
        i += 1;
        outBuf[i] = header->addCommandSym;
        i += 1;
        outBuf[i] = (uint8_t) 0xffu;
        i += 1;

        debug("Serializing the data head (index = %d)\n", i);
        memcpy(&outBuf[i], header->dataChunkName, 4);
        i += 4;
        littleSerialize(&outBuf[i], 8, 0, &header->dataSize);
        i += 8;
        debug("Wrote %d bytes for V1 header\n", i);
        if(i != outSize) {
                error("Error while serializing header: Expected to write %d bytes; wrote %d\n", 
                        outSize, i);
                return EXIT_FAILURE;
        }

        // Writing the serialized buffer to the output file
        verbose("Writing v1 header to out file\n");
        const int written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize || ferror(outFile)) {
                error("Error while writing header: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                return EXIT_FAILURE;
        }

        free(outBuf);
        return EXIT_SUCCESS;
}

uint32_t writeHeader(const struct Slurped *args, FILE *outFile, uint64_t dataSize) 
{
        struct Version1Header header = {
                .magicNumber    = MAGIC_NUMBER,
                .versPseudoChunk   = VERSION_CHUNK_NAME,
                .versionId      = 1,

                .metaChunk      = META_CHUNK_NAME,
                .metaSize       = META_V1_SIZE,
                .sourceHash     = NULL_SHA,
                .targetHash     = NULL_SHA,
                .targetSize     = -1, // NOTE: Overridden in a moment
                .moveCommandSym = MOVE_COMMAND,
                .addCommandSym  = ADD_COMMAND,
                .cmdLensEqual   = 0,
                // padding is always serialized as 0xff in outputHeaderV1

                .dataChunkName  = DATA_CHUNK_NAME,
                .dataSize       = dataSize
        };

        // Getting file length 
        verbose("Opening target as readonly (to get its size)\n");
        FILE *tgt = attemptRFileOpen(args->posArg2, args->posArg2Len);
        if (tgt == NULL) {
                verbose("Cancelled header write\n");
                return EXIT_FAILURE;
        }

        header.targetSize.longVal = fileLength(tgt);

        verbose("Target Size: %d bytes\n", header.targetSize.longVal);
        verbose("Closing the aforementioned read-only target file\n");
        if (fclose(tgt) != 0) {
                error("Error while formatting header: Could not close target file\n");
                loud(" \\___ Reason: %s\n", strerror(ferror(tgt)));
                return EXIT_FAILURE;
        }

        // Generating file hashes
        // FIXME: We don't currently support file hashing here, so we default to ignore
        debug("Ignore hash flag: %08x\n", args->flags & IGNORE_HASH_FLAG);
        if (1 || (args->flags & IGNORE_HASH_FLAG)) {
                loud("Warning: Ignoring hash generation for source and target files\n");
                normal(" \\___ The reconstructed target could possibly be silently corrupted as a result\n");
                // TODO: Warnings-as-errors flag
        }

        // Writing the header
        verbose("Writing the header to the output file\n");
        return outputHeaderV1(outFile, &header);
}

uint64_t serializeCmds(uint8_t *outBuf, uint64_t bufSize, 
        struct LinkedCommand *head) 
{
        struct LinkedCommand *cur = head->next;
        uint64_t i = 0;

        while (cur != NULL) {
                const struct Command *cmd = cur->elem;
                verbose(" \\___ Serializing command with type '%c' and serial size %d\n",
                        cmd->type, serialSizeOf(cmd));  
                debug(" \\___ Index: %d\n", i);
                
                // Serializing the command
                const uint32_t expectedSize = serialSizeOf(cmd);
                const uint32_t wrote = serializeCommand(outBuf, bufSize, i, cmd);

                if (wrote != expectedSize) {
                        error("Error while serializing: Expected to write %d bytes for command type '%c'; wrote %d\n",
                                expectedSize, cmd->type, wrote);
                        return i;
                }

                i += expectedSize;                
                cur = cur->next;
        }

        verbose("Finished serialization\n");
        return i;
}

int computeDelta(const struct Slurped *args) 
{        
        
        // Getting our output file
        // This is done first to prevent postponing any errors or prompting
        // for a time when the user has already waited minutes (or hours)
        // for delta computation to have finished
        FILE *outFile = attemptWFileOpen(args->outputFileName, args->outputLen, 
                args->flags);
        if (outFile == NULL) {
                verbose("Cancelled the delta computation.\n");
                return EXIT_FAILURE;
        }

        // Computing our commands 
        struct LinkedCommand head = { .elem = NULL, .next = NULL };
        const uint64_t outSize = computeCmdsFromArgs(args, &head);
        if (outSize == 0) {
                verbose("Cancelled the delta computation.\n");
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        
        // Generating a header, and writing it to our file
        const uint32_t headRet = writeHeader(args, outFile, outSize);
        if (headRet != EXIT_SUCCESS) {
                verbose("Cancelled the delta computation\n");
                freeLinked(head.next);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }

        // Allocating our destination for serialization
        normal("Serializing the commands... (this may take a while)\n");
        debug("Allocating %d bytes...\n", outSize);
        uint8_t *outBuf = (uint8_t *) malloc(outSize);
        debug("Allocated.\n");
        if (outBuf == NULL) {
                error("Error while serializing: Could not allocate %d byte output buffer\n",
                        outSize);
                freeLinked(head.next);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }

        // Serializing the commands
        if(serializeCmds(outBuf, outSize, &head) != outSize) {
                // Error message was already sent from serializeCmds()
                verbose("Cancelled delta computation\n");
                freeLinked(head.next);
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        freeLinked(head.next);

        // Writing the serialized output to a file.
        normal("Writing the serialized commands buffer to a file...\n");
        const int written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize || ferror(outFile)) {
                error("Error while writing delta: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE; // TODO: Error code
        }

        // Cleaning up
        normal("Finished outputing the delta\n");
        free(outBuf);
        fclose(outFile); // Doesn't really matter if this fails
        return EXIT_SUCCESS;
}