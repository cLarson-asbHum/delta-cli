#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "delta-command.h"
#include "delta.h"
#include "file-helper.h"
#include "hash.h"

#define V1_COMPAT_OFFSET 0

struct LinkedCommand {
        struct Command *elem;
        struct LinkedCommand *next;
};

uint32_t freeLinked(struct LinkedCommand *head) {
        struct LinkedCommand *cur = head;
        uint32_t i = 0;
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
        if (command->type == ADD_COMMAND && (getLogLevel() >= VERBOSE_LVL)) {
                const char c = (char) (command->cmd.add.symbol);
                const uint64_t qSet = command->cmd.add.tgtIndex.longVal;
                verbose("   \\___ Command: ADD '%c' at %llu \n", c, qSet);
                return ;
        }

        if (command->type == ADD_64_COMMAND && (getLogLevel() >= VERBOSE_LVL)) {
                const uint64_t c = (uint64_t) (command->cmd.add64.symbol64);
                const uint64_t qSet = command->cmd.add.tgtIndex.longVal;
                verbose("   \\___ Command: ADD_64 0x%016llx at %llu \n", c, qSet);
                return ;
        }

        if (command->type == MOVE_COMMAND && (getLogLevel() >= VERBOSE_LVL)) {
                const uint64_t pSet = command->cmd.move.srcIndex.longVal;
                const uint64_t qSet = command->cmd.move.tgtIndex.longVal;
                const uint64_t l    = command->cmd.move.len.longVal;
                verbose("   \\___ Command: MOVE %llu -> %llu (length %llu) \n", 
                        pSet, qSet, l);
                return ;
        }

        // Garbage data
        verbose("   \\___ Garbage command (type <%llu>)\n", command->type);
}

void debugLinked(struct LinkedCommand *head) 
{
        if (DEBUG) {
                struct LinkedCommand *cur = head;
                uint32_t i = 0;
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

// Computes exclusively MOVE and ADD commands, and thus is guaranteed to be 
// v1 compatible. This returns 0 if an error is caught. The generated commands 
// are appended onto the head with first-in-first-out order (as compared to the 
// usual first-in-last-out).
uint64_t computeCmds(const struct FileBin *s, const struct FileBin *t, 
        uint64_t tStart64, struct LinkedCommand *head) 
{
        struct LinkedCommand *last = head;
        uint64_t q = tStart64;
        uint64_t outSize = 0;

        while (q < t->size) {
                loud(" \\___ %llu / %llu (%.2f%%)\n", q, t->size, 
                        100.0f * (float) q / (float) t->size);

                // WARNING: We assume that command is never NULL, which assumes
                //          that computeCmds was bound checked before its invocation
                struct Command *command = nextLargestMove(s->buf, 0, s->size, 
                        &(t->buf[q]), t->size - q);

                switch (command->type) {
                case ADD_COMMAND:
                        command->cmd.add.tgtIndex.longVal = q;
                        break;
                case MOVE_COMMAND:
                        command->cmd.move.tgtIndex.longVal = q;
                        break;
                // We don't include a default, as it can never fail (see WARNING above)
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

// Computes MOVE, ADD, and ADD_64 commands. If the lengths of s and t are not multiples of 8,
// this will invoke computeCmds() on the last 1-7 bytes of the two strings. This 
// returns 0 if an error is caught. The generated commands are appended onto the onto the head 
// with first-in-first-out order (as compared to the usual first-in-last-out).
uint64_t computeCmds64(const struct FileBin *s, const struct FileBin *t, 
        uint64_t tStart, struct LinkedCommand *head) 
{
        const uint64_t *s64 = (uint64_t *) s->buf;
        const uint64_t *t64 = (uint64_t *) t->buf;
        const uint64_t s64Size = s->size >> 3;
        const uint64_t t64Size = t->size >> 3;

        struct LinkedCommand *last = head;
        uint64_t q = tStart;
        uint64_t outSize = 0;

        while (q + 7 < t->size && s64Size != 0) {
                info(" \\___ %llu / %llu (%.2f%%)\n", q, t->size, 
                        100.0f * (float) q / (float) t->size);

                // WARNING: We assume that command is never NULL, which assumes
                //          that computeCmds was bound checked before its invocation
                struct Command *command = nextLargest64Move(s64, s64Size, 
                        &t64[q >> 3], t64Size - (q >> 3));

                switch (command->type) {
                case ADD_COMMAND:
                        command->cmd.add.tgtIndex.longVal = q;
                        break;
                case MOVE_COMMAND:
                        command->cmd.move.tgtIndex.longVal = q;
                        break;
                case ADD_64_COMMAND:
                        command->cmd.add64.tgtIndex.longVal = q;
                        break;
                // We don't include a default, as it can never fail (see WARNING above)
                }
                
                verboseCmdLog(command);
                q += patchSizeOf(command);
                outSize += serialSizeOf(command);

                // Verifying that we only could've landed on a multiple of 8
                if ((q & 7) != 0) {
                        error("Garbage state while computing delta: Index in target did not cleanly land on a uint64_t (index was %llu)\n",
                                q);
                        freeLinked(head->next);
                        head->next = NULL;
                        return 0;
                }

                // Appending our command to the end of a singly-linked list.
                // We add to the last elem rather than the head in order to
                // have first-in-first-out iteration.
                struct LinkedCommand *append = malloc(sizeof(struct LinkedCommand));
                append->elem = command;
                append->next = NULL;
                last->next = append; // Appending our node onto the last node
                last = append; // Making our node the last node.
        }

        // Handling any uncompared bytes at the end
        if ((t->size & 7) != 0 && s->size != 0) {
                debug("Parsing the end %d bytes of T separately\n", t->size & 7);
                debug(" \\___ Starting at index %llu of T\n", t64Size << 3);
                outSize += computeCmds(s, t, t64Size << 3, last);
        }

        return outSize;
}

// Single-pivot Quicksort implementation that sorts the indices of arr by the 
// corresponding value in arr at that index. 
//
// This is a direct usage of a C implementation of Quicksort found in Wikibooks:
// 
//  > Algorithm Implementation/Sorting/Quicksort. (2025, December 21). Wikibooks. 
//  > Retrieved August 19, 2026, from 
//  > https://en.wikibooks.org/w/index.php?title=Algorithm_Implementation/Sorting/Quicksort&oldid=4608421.
void sortIndices(const uint8_t *arr, struct UintNArray *indices, int64_t beg, 
        int64_t end)
{
        uint64_t piv; // Quicksort pivot
        uint64_t tmp; // Swap variable

        uint64_t l; // Left comparison index (for when we swap based on the pivot)
        uint64_t r; // Right comparison index (for when we swap based on the pivot)
        uint64_t p; // Index of the pivot

        double rt;

        // TODO: Figure out why changing the beg and end types to uint64_t 
        //       causes the comparison to change their values. It might be a compiler error
        while (beg < end) { // This while loop will avoid the second recursive call
                //#region DEV START: Logging recursion
                uint8_t didLog = 0;
                if ((end - beg + 1) / 1024 > 100) {
                        debug("[size=%llu KiB] ", (end - beg + 1) / 1024);
                        didLog = 1;
                }
                //#endregion DEV END
                
                l = beg;
                p = (end + beg) / 2; // TODO: Make this random (we keep getting stuck)
                r = end;

                // TODO: Decide if a faster pivot would help
                piv = READ_64(arr[readUint(indices,p)]);

                // Swapping smaller elems to the left, and larger to the right
                while (1) {
                        // Skip over any elements that are already on the correct
                        // side of the pivot.
                        while ((l <= r) && (READ_64(arr[readUint(indices,l)]) <= piv)) {
                                l++;
                        }
                        
                        while ((l <= r) && (READ_64(arr[readUint(indices,r)]) > piv)) {
                                r--;
                        }

                        if (l > r)
                                break;

                        // The values at indices l and r were not on the correct
                        // side of the pivot; swap them.
                        tmp = readUint(indices, l);
                        writeUint(indices, l, readUint(indices, r));
                        writeUint(indices, r, tmp);

                        if (p == r) {
                                p = l;
                        }

                        l++;
                        r--;
                }

                // Ordering the pivot itself
                tmp = readUint(indices, p);
                writeUint(indices, p, readUint(indices, r));
                writeUint(indices, r, tmp);
                r--;

                // Recursion on the shorter side & loop (with new indexes) on 
                // the longer
                if ((r - beg) < (end - l)) {
                        sortIndices(arr, indices, beg, r);
                        beg = l;
                } else {
                        sortIndices(arr, indices, l, end);
                        end = r;
                }
                //#region DEV START: Logging "recursion"
                if (didLog) {
                        debug("[fin]");
                }
                //#endregion DEV END
        }

}

// Returns 1 if any index in sorted is out of order; 0 otherwise (or length is 
// less than 2)
uint64_t notSorted(const uint8_t *arr, const struct UintNArray *indices, 
        uint64_t len) 
{
        uint64_t last = READ_64(arr[readUint(indices, 0)]);
        for (uint64_t i = 1; i < len; i++) {
                const uint64_t cur = READ_64(arr[readUint(indices, i)]);
                if(cur < last) {
                        error("Garbage state: Sorted indices using quicksort, yet index %llu was still out of place\n", 
                                i);
                        return 1;
                }
        }
        return 0;
}
 
void populateIndices(struct UintNArray *indices, uint64_t len) 
{
        for (uint64_t i = 0; i < len; i++) {
                // TODO: Make writeUint() fast
                writeUint(indices, i, i);
        }
}

// Computes MOVE, ADD, and ADD_64 commands. If the lengths of s and t are not 
// multiples of 8, this will invoke computeCmds() on the last 1-7 bytes of the 
// two strings. This  returns 0 if an error is caught. The generated commands 
// are appended onto the onto the head  with first-in-first-out order (as 
// compared to the usual first-in-last-out).
//
// This has an asymptotically faster avg time complexity compared to 
// computeCmds64(), as the largestMove routine uses binary sort to achieve 
// O(m log m); Quicksort is used to guarantee that the list is sorted. The 
// average time complexity of this algorithm is Ω((m + n) log m) where m is 
// the number of uint64s in the target, and n is the number of uints64s in the 
// source.
//
// The memory usage of this function is greater larger than computeCmds64() 
// because extra memory is allocated to store sorted indices.
uint64_t computeCmds64Q(const struct FileBin *s, const struct FileBin *t, 
        uint64_t tStart, struct LinkedCommand *head) 
{
        const uint64_t sLen = s->size;
        const uint64_t *t64 = (uint64_t *) t->buf;
        const uint64_t t64Size = t->size >> 3;
        struct LinkedCommand *last = head; // Where commands are appended

        // Initializing our indices used for sorting
        detail(" \\___ Initializing indices for sorting...\n");
        struct UintNArray sorted = { .bytes = NULL, .byteWidth = calcWidth(sLen) };
        sorted.bytes = malloc(sLen * sorted.byteWidth * sizeof(uint8_t));

        if (sorted.bytes == NULL) {
                error("Error while computing delta: Could not allocate %llu bytes for sorting\n",
                        sLen * sorted.byteWidth * sizeof(uint8_t));
                return 0;
        }

        info(" \\___ Populating indices for sorting...\n");
        populateIndices(&sorted, sLen);
        loud(" \\___ Sorting indices to our file... (this takes a long time)\n");
        // sortIndices(s->buf, &sorted, 0, sLen - 1);
        sortIndices(s->buf, &sorted, 0, sLen - 1);

        verbose(" \\___ Verifying sortedness...\n");
        if(notSorted(s->buf, &sorted, sLen)) {
                free(sorted.bytes);
                return 0;
        }

        // Computing our commands (but FASTER 🤘)
        uint64_t q = tStart;
        uint64_t outSize = 0;
        while (q + 7 < t->size && sLen != 0) {
                info(" \\___ %llu / %llu (%.2f%%)\n", q, t->size, 
                        100.0f * (float) q / (float) t->size);

                // WARNING: We assume that command is never NULL, which assumes
                //          that computeCmds was bound checked before its invocation
                struct Command *command = nextLargest64Sorted(s->buf, &sorted, 
                        sLen, &t64[q / 8], t64Size - (q / 8));

                switch (command->type) {
                case ADD_COMMAND:
                        command->cmd.add.tgtIndex.longVal = q;
                        break;
                case MOVE_COMMAND:
                        command->cmd.move.tgtIndex.longVal = q;
                        break;
                case ADD_64_COMMAND:
                        command->cmd.add64.tgtIndex.longVal = q;
                        break;
                // We don't include a default, as it can never fail (see WARNING above)
                }
                
                verboseCmdLog(command);
                q += patchSizeOf(command);
                outSize += serialSizeOf(command);

                // Verifying that we only could've landed on a multiple of 8
                if ((q & 7) != 0) {
                        error("Garbage state while computing delta: Index in target did not cleanly land on a uint64_t (index was %llu)\n",
                                q);
                        freeLinked(head->next);
                        free(sorted.bytes);
                        head->next = NULL;
                        return 0;
                }

                // Appending our command to the end of a singly-linked list.
                // We add to the last elem rather than the head in order to
                // have first-in-first-out iteration.
                struct LinkedCommand *append = malloc(sizeof(struct LinkedCommand));
                append->elem = command;
                append->next = NULL;
                last->next = append; // Appending our node onto the last node
                last = append; // Making our node the last node.
        }

        // Handling any uncompared bytes at the end
        if ((t->size & 7) != 0 && sLen != 0) {
                debug("Parsing the end %d bytes of T separately\n", t->size & 7);
                debug(" \\___ Starting at index %llu of T\n", t64Size << 3);
                outSize += computeCmds(s, t, t64Size << 3, last);
        }

        free(sorted.bytes);
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
        int16_t version = CURRENT_VERSION;
        if (args->flags & FILE_VERSION_FLAG) {
                version = args->version;
        }


        info("Computing ... (takes a lot of time)\n");
        uint64_t outSize = 0;
        switch (version) {
        case 1: 
                outSize = computeCmds(s, t, 0, head);
                break;
        case 2: 
                // TODO: Flag for using sorted version (or negation)
                outSize = computeCmds64Q(s, t, 0, head);
                break;
        default: 
                outSize = 0;
                break;
        }
        debugLinked(head);
        freeBin(s);
        freeBin(t);
        return outSize;
}

// Supplies values for the v1 fields
int32_t hydrateHeaderV1(const struct Slurped *args, struct Version1Header *v1) 
{
        const struct Version1Header ideal = {
                .metaChunk      = META_CHUNK_NAME,
                .metaSize       = V1_META_SIZE,
                .sourceHash     = NULL_SHA,
                .targetHash     = NULL_SHA,
                .cmdLensEqual   = 0
        };
        memcpy(v1->metaChunk, ideal.metaChunk, 4);
        v1->metaSize    = ideal.metaSize;
        v1->sourceHash  = ideal.sourceHash;
        v1->targetHash  = ideal.targetHash;
        v1->cmdLensEqual = ideal.cmdLensEqual;
        // padding is always serialized as 0xff in outputHeaderV1

        // Generating file hashes
        if (!(args->flags & IGNORE_HASH_FLAG)) {
                // The target file
                FILE *tgt = attemptRFileOpen(args->posArg2, args->posArg2Len);
                if (tgt == NULL) {
                        verbose("Cancelled header write\n");
                        return EXIT_FAILURE;
                }

                info("Computing file hash for the target...\n");
                if (computeFileHash(&v1->targetHash, tgt) == 0) {
                        verbose("Cancelled header write\n");
                        fclose(tgt);
                        return EXIT_FAILURE;
                }
                fclose(tgt); // Doesn't really matter if this fails

                // The source file
                FILE *src = attemptRFileOpen(args->posArg1, args->posArg1Len);
                if (src == NULL) {
                        verbose("Cancelled header write\n");
                        fclose(tgt); // Doesn't matter if it fails
                        return EXIT_FAILURE;
                }

                info("Computing file hash for the source...\n");
                if (computeFileHash(&v1->sourceHash, src) == 0) {
                        verbose("Cancelled header write\n");
                        fclose(src); // Doesn't really matter if this fails
                        return EXIT_FAILURE;
                }
                fclose(src); // Doesn't really matter if this fails
        }

        return EXIT_SUCCESS;
}

int32_t hydrateHeaderV2(const struct Slurped *args, struct Version2Header *v2,
        uint32_t isV1Compatible) 
{
        const struct Version2Header ideal = {
                .metaChunk      = META_CHUNK_NAME,
                .metaSize       = V1_META_SIZE,
                .sourceHash     = NULL_SHA,
                .targetHash     = NULL_SHA,
                .isV1Compatible = isV1Compatible
        };
        memcpy(v2->metaChunk, ideal.metaChunk, 4);
        v2->metaSize    = ideal.metaSize;
        v2->sourceHash  = ideal.sourceHash;
        v2->targetHash  = ideal.targetHash;
        v2->isV1Compatible = ideal.isV1Compatible;

        // Generating file hashes
        // FIXME: We don't currently support file hashing here, so we default to ignore
        
        // Generating a file hash for the target and source
        if (!(args->flags & IGNORE_HASH_FLAG)) {
                // The target file
                FILE *tgt = attemptRFileOpen(args->posArg2, args->posArg2Len);
                if (tgt == NULL) {
                        verbose("Cancelled header write\n");
                        return EXIT_FAILURE;
                }

                info("Computing file hash for the target...\n");
                if (computeFileHash(&v2->targetHash, tgt) == 0) {
                        verbose("Cancelled header write\n");
                        fclose(tgt);
                        return EXIT_FAILURE;
                }
                fclose(tgt); // Doesn't really matter if this fails

                // The source file
                FILE *src = attemptRFileOpen(args->posArg1, args->posArg1Len);
                if (src == NULL) {
                        verbose("Cancelled header write\n");
                        fclose(tgt); // Doesn't matter if it fails
                        return EXIT_FAILURE;
                }

                info("Computing file hash for the source...\n");
                if (computeFileHash(&v2->sourceHash, src) == 0) {
                        verbose("Cancelled header write\n");
                        fclose(src); // Doesn't really matter if this fails
                        return EXIT_FAILURE;
                }
                fclose(src); // Doesn't really matter if this fails
        }


        return EXIT_SUCCESS;
}

int32_t hydrateHeader(const struct Slurped *args, struct DeltaHeader *header, 
        int16_t version, uint32_t headerFlags) 
{
        switch (version) {
        case 1:
                return hydrateHeaderV1(args, &header->header.v1);
        case 2:
                return hydrateHeaderV2(args, &header->header.v2, 
                        (headerFlags >> V1_COMPAT_OFFSET) & 1);
        default:
                error("Error while formatting header: Version %d is unknown (min=1; max=%d)\n",
                        version, CURRENT_VERSION);
                return EXIT_FAILURE;
        }
}

// Returns EXIT_SUCCESS if successful; EXIT_FAILURE otherwise. Error messages are
// printed on error
int32_t outputHeaderV1(FILE *outFile, const struct DeltaHeader *header)
{
        const uint64_t outSize = V1_DELTA_HEADER_SIZE;
        debug("Allocating %llu bytes for header\n", outSize);
        uint8_t *outBuf = malloc(outSize);

        if (outBuf == NULL) {
                error("Error while serializing header: Could not allocate %llu bytes\n", 
                        outSize);
                return EXIT_FAILURE;
        }

        // Serializing the header
        // WARNING: We assume all the header data is initialized and outBuf is large enough
        detail("Serializing v1 header\n");
        debug("Serializing the top level data (index = 0)\n");
        uint32_t i = 0;
        memcpy(&outBuf[i], header->magicNumber,  4);
        i += 4;
        memcpy(&outBuf[i], header->versPseudoChunk, 3);
        i += 3;
        outBuf[i] = header->versionId;
        i += 1;
        littleSerialize(&outBuf[i], 8, 0, &header->targetSize);
        i += 8;

        const struct Version1Header v1 = header->header.v1; 
        debug("Serializing the meta chunk (index = %d)\n", i);
        memcpy(&outBuf[i],  v1.metaChunk, 4);
        i += 4;
        littleSerialize(&outBuf[i], 8, 0, &v1.metaSize);
        i += 8;
        memcpy(&outBuf[i], v1.sourceHash.bytes, 32);
        i += 32;
        memcpy(&outBuf[i], v1.targetHash.bytes, 32);
        i += 32;
        outBuf[i] = v1.cmdLensEqual;
        i += 1;
        for(uint32_t p = 0; p < V1_META_PADDING; p++) {
                outBuf[i] = (uint8_t) 0xffu;
                i++;
        }

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
        detail("Writing v1 header to out file\n");
        const uint32_t written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize || ferror(outFile)) {
                error("Error while writing header: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                return EXIT_FAILURE;
        }

        free(outBuf);
        return EXIT_SUCCESS;
}

// Returns EXIT_SUCCESS if successful; EXIT_FAILURE otherwise. Error messages are
// printed on error
int32_t outputHeaderV2(FILE *outFile, const struct DeltaHeader *header)
{
        const uint64_t outSize = V2_DELTA_HEADER_SIZE;
        debug("Allocating %llu bytes for header\n", outSize);
        uint8_t *outBuf = malloc(outSize);

        if (outBuf == NULL) {
                error("Error while serializing header: Could not allocate %llu bytes\n", 
                        outSize);
                return EXIT_FAILURE;
        }

        // Serializing the header
        // WARNING: We assume all the header data is initialized and outBuf is large enough
        verbose("Serializing v2 header\n");
        debug("Serializing the top level data (index = 0)\n");
        uint32_t i = 0;
        memcpy(&outBuf[i], header->magicNumber,  4);
        i += 4;
        memcpy(&outBuf[i], header->versPseudoChunk, 3);
        i += 3;
        outBuf[i] = header->versionId;
        i += 1;
        littleSerialize(&outBuf[i], 8, 0, &header->targetSize);
        i += 8;

        const struct Version2Header v2 = header->header.v2; 
        debug("Serializing the meta chunk (index = %d)\n", i);
        memcpy(&outBuf[i],  v2.metaChunk, 4);
        i += 4;
        littleSerialize(&outBuf[i], 8, 0, &v2.metaSize);
        i += 8;
        memcpy(&outBuf[i], v2.sourceHash.bytes, 32);
        i += 32;
        memcpy(&outBuf[i], v2.targetHash.bytes, 32);
        i += 32;
        outBuf[i] = v2.isV1Compatible;
        i += 1;
        for(uint32_t p = 0; p < V1_META_PADDING; p++) {
                outBuf[i] = (uint8_t) 0xffu;
                i++;
        }

        debug("Serializing the data head (index = %d)\n", i);
        memcpy(&outBuf[i], header->dataChunkName, 4);
        i += 4;
        littleSerialize(&outBuf[i], 8, 0, &header->dataSize);
        i += 8;
        debug("Wrote %d bytes for V2 header\n", i);
        if(i != outSize) {
                error("Error while serializing header: Expected to write %d bytes; wrote %d\n", 
                        outSize, i);
                return EXIT_FAILURE;
        }

        // Writing the serialized buffer to the output file
        verbose("Writing v2 header to out file\n");
        const uint32_t written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize || ferror(outFile)) {
                error("Error while writing header: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                return EXIT_FAILURE;
        }

        free(outBuf);
        return EXIT_SUCCESS;
}

int32_t outputHeader(FILE *outFile, const struct DeltaHeader *header, 
        int16_t version) 
{
        switch (version) {
        case 1:
                return outputHeaderV1(outFile, header);
        case 2:
                return outputHeaderV2(outFile, header);
        default: 
                error("Garbage state: version %d was unknown and was not caught earlier\n");
                return EXIT_FAILURE;
        }
}

int32_t writeHeader(const struct Slurped *args, FILE *outFile, uint64_t dataSize,
        uint32_t headerFlags) 
{
        int16_t version = CURRENT_VERSION;
        if (args->flags & FILE_VERSION_FLAG) {
                version = args->version;
        }

        struct DeltaHeader header = {
                .magicNumber     = MAGIC_NUMBER,
                .versPseudoChunk = VERSION_CHUNK_NAME,
                .versionId       = version,
                .targetSize      = -1, // Gets overridden a few lines down
                // .union gets overridden several lines down
                .dataChunkName   = DATA_CHUNK_NAME,
                .dataSize        = dataSize
        };

        // Getting file length 
        verbose("Opening target as readonly (to get its size)\n");
        FILE *tgt = attemptRFileOpen(args->posArg2, args->posArg2Len);
        if (tgt == NULL) {
                verbose("Cancelled header write\n");
                return EXIT_FAILURE;
        }

        header.targetSize.longVal = fileLength(tgt);
        verbose("Target Size: %llu bytes\n", header.targetSize.longVal);
        verbose("Closing the aforementioned read-only target file\n");
        if (fclose(tgt) != 0) {
                error("Error while formatting header: Could not close target file\n");
                loud(" \\___ Reason: %s\n", strerror(ferror(tgt)));
                return EXIT_FAILURE;
        }

        // Hydrating the version-specific fields
        if (hydrateHeader(args, &header, version, headerFlags) != EXIT_SUCCESS) {
                verbose("Cancelling header format\n");
                return EXIT_FAILURE;
        }

        // Writing the header
        verbose("Writing the header to the output file\n");
        return outputHeader(outFile, &header, version);
}

uint64_t serializeCmds(uint8_t *outBuf, uint64_t bufSize, 
        struct LinkedCommand *head) 
{
        struct LinkedCommand *cur = head->next;
        uint64_t i = 0;

        while (cur != NULL) {
                const struct Command *cmd = cur->elem;
                detail(" \\___ Serializing command with type '%c' and serial size %d\n",
                        cmd->type, serialSizeOf(cmd));  
                debug(" \\___ Index: %llu\n", i);
                
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

// Returns 1 if all the commands in the linked list are those that can be 
// parsed in v1, and 0 otherwise. No commands in the linked list returns 1.
uint8_t isV1Compatible(struct LinkedCommand *head) 
{
        struct LinkedCommand *cur = head;
        int32_t i = 0;
        while (cur != NULL) {
                if (cur->elem != NULL && minVersion(cur->elem->type) != 1) {
                        verbose("Command %d are NOT v1 compatible\n", i - 1);
                        return 0;
                }
                cur = cur->next;
                i++;
        }
        verbose("The commands are v1 compatible\n");
        return 1;
}

int32_t computeDelta(const struct Slurped *args) 
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
        verbose("Getting information about our commands for the file header...\n");
        uint32_t headerFlags = 0; // Flags that contain data for the header
        headerFlags = (headerFlags | (isV1Compatible(&head) << V1_COMPAT_OFFSET));
        verbose("Formatting the header...\n");
        if (writeHeader(args, outFile, outSize, headerFlags) != EXIT_SUCCESS) {
                verbose("Cancelled the delta computation\n");
                freeLinked(head.next);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }

        // Allocating our destination for serialization
        info("Serializing the commands... (this may take a while)\n");
        debug("Allocating %llu bytes...\n", outSize);
        uint8_t *outBuf = (uint8_t *) malloc(outSize);
        debug("Allocated.\n");
        if (outBuf == NULL) {
                error("Error while serializing: Could not allocate %llu byte output buffer\n",
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
        info("Writing the serialized commands buffer to a file...\n");
        if (fwrite(outBuf, 1, outSize, outFile) != outSize || ferror(outFile)) {
                error("Error while writing delta: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE; // TODO: Error code
        }

        // Cleaning up
        detail("Finished outputing the delta\n");
        free(outBuf);
        fclose(outFile); // Doesn't really matter if this fails
        return EXIT_SUCCESS;
}