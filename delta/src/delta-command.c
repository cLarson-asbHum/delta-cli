#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "delta-command.h"
#include "delta.h"
#include "delta-cli.h"

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
                const uint8_t qSet = command->cmd.add.curIndex.longVal;
                verbose("   \\___ Command: ADD '%c' at %d \n", c, qSet);
                return ;
        }

        if (command->type == MOVE_COMMAND && (getLogFlags() & VERBOSE_FLAG)) {
                const uint64_t pSet = command->cmd.move.prevIndex.longVal;
                const uint64_t qSet = command->cmd.move.curIndex.longVal;
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
                        command->cmd.add.curIndex.longVal = q;
                } else {
                        command->cmd.move.curIndex.longVal = q;
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

        // TODO: Format header
        // TODO: Write header

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