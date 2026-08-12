#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "slurp-arg.h"
#include "log-level.h"
#include "file-helper.h"
#include "hash.h"
#include "delta.h"

union SerialDword {
        char str[5]; // Extra char for a null terminator
        uint32_t dword;
};

// Returns the index of the specified chunk, 0 if an error occurs
uint64_t findChunk(const struct FileBin *delta, uint64_t start, 
        const char chunkName[4], union SerialLong *chunkSize) 
{
        if (delta->size < 11uLL) {
                // Previous code should always have caught this, but we 
                // check just in case some idiot programmer (me) forgets 
                // to check
                return 0;
        }

        union SerialDword exp;
        memcpy(exp.str, chunkName, 4);
        exp.str[4] = '\0';

        const uint8_t *buf = delta->buf;
        const uint64_t bufSize = delta->size;
        uint8_t found = 0;

        do {
                // Comparing the chunk name against the specified
                union SerialDword curName = { .str = {0,0,0,0,0} };
                memcpy(curName.str, &buf[start], 4);
                verbose("Byte %llu: Checking whether '%s' is '%s'\n", start, 
                        curName.str, exp.str);
                found = (curName.dword == exp.dword);
                start += 4;

                // Getting the chunk size
                littleDeserialize(buf, bufSize, start, chunkSize);
                debug("Byte %llu: Chunk size = %llu\n", start, chunkSize->longVal);
                start += 8;

                // Skipping this current chunk if it isn't the meta chunk
                if(!found) {
                        loud("Warning: Encountered unexpected chunk \"%s\". It has been skipped\n",
                                curName.str);
                        normal(" \\___ Was expecting \"%s\"\n", exp.str);
                        verbose(" \\___ Byte %llu: Skipping %llu bytes\n", start, 
                                chunkSize->longVal);
                        start += chunkSize->longVal;
                }
        } while (!found && start < bufSize - 11uLL);

        if (!found) {
                error("Error while reading delta: Could not find chunk '%s'.\n", 
                        exp.str);
                return 0;
        } 

        if(start + chunkSize->longVal > bufSize) {
                error("Error while reading delta: Meta chunk size (%llu bytes) extends out of bounds\n",
                        chunkSize->longVal);
                return 0;
        }

        return start;
}

// Returns the index at which commands start, i.e. the index after 
// DeltaHeader.dataSize. If an error occurs, this returns 0; this shouldn't be 
// misinterpreted as an actual index, as a delta file MUST have at least 
// 'DLTA' at the start, which guarantees that index 0 is NOT a command.
uint64_t readAndVerifyV1(const struct Slurped *args, struct DeltaHeader *header, 
        const struct FileBin *delta, uint64_t start)
{
        if (delta->size < V1_DELTA_HEADER_SIZE) {
                error("Error while reading delta: Delta file's header is too small (size < %d bytes)\n",
                        V1_DELTA_HEADER_SIZE);
                normal(" \\___ A delta generated with the --preserve flag may be incomplete because of an error\n");
                return 0;
        }

        struct Version1Header *v1 = &header->header.v1;

        // Searching for the meta chunk.
        // If there is a chunk we don't recognize, we skip it and check the next
        const char met[4] = META_CHUNK_NAME;
        start = findChunk(delta, start, met, &v1->metaSize);

        if (start == 0) {
                // Error would've already been printed to stderr.
                return 0;
        } 
        
        if (v1->metaSize.longVal < V1_META_SIZE) {
                error("Error while reading delta: Meta chunk size is not large enough (%llu bytes < %d bytes)\n",
                        v1->metaSize.longVal, V1_META_SIZE);
                return 0;
        }

        if (v1->metaSize.longVal > V1_META_SIZE) {
                loud("Warning: Meta chunk size is larger than expected (%llu bytes > %d bytes)\n",
                        v1->metaSize.longVal, V1_META_SIZE);
                normal(" \\___ Some data may be erroneously skipped\n");
                // TODO: Warning as errors
        }

        const uint64_t metaDataStart = start;

        // Getting the source and target hashes
        for (uint8_t i = 0; i < 32; i++) {
                v1->sourceHash.bytes[i] = delta->buf[start + i];
                v1->targetHash.bytes[i] = delta->buf[start + i + 32];
        }
        start += 64;

        // Skipping cmdLensEqual and the padding bytes
        // We ignore the cmdLensEqual member, as using it would increase the 
        // complexity of the program (even though it would increase speed)
        start += 4;

        // Skipping to the end of the meta chunk
        // This won't go out of bounds, as that will have been caught by findChunk()
        start = metaDataStart + v1->metaSize.longVal;

        // Searching for the data chunk.
        // If there is a chunk we don't recognize, we skip it and check the next
        const char dat[4] = DATA_CHUNK_NAME;
        start = findChunk(delta, start, dat, &header->dataSize);
        
        if(start == 0) {
                // Error message already printed.
                return 0;
        }

        if (header->dataSize.longVal < 1) {
                error("Error while reading delta: Data chunk size is not large enough (%llu bytes < %d bytes)\n",
                        header->dataSize.longVal, 1);
                return 0;
        }

        return start;
}

// Determines whether the chunk names are equal
uint8_t chunkNamesEq(const char name1[4], const char name2[4]) 
{
        debug("Comparing %d to %d\n", *((uint32_t *) name1), 
                *((uint32_t *) name2));
        return *((uint32_t *) name1) == *((uint32_t *) name2);
}

// Returns the index at which commands start, i.e. the index after 
// DeltaHeader.dataSize. If an error occurs, this returns 0; this shouldn't be 
// misinterpreted as an actual index, as a delta file MUST have at least 
// 'DLTA' at the start, which guarantees that index 0 is NOT a command.
uint64_t readAndVerifyHeader(const struct Slurped *args, 
        struct DeltaHeader *header, const struct FileBin *delta)
{
        // 16 bytes is enough for the magic num, the version, and 
        // the target size
        if (delta->size < 16uLL) {
                error("Error while reading delta: File was too small (< %d bytes)\n",
                        16);
                return 0;
        }

        uint64_t start = 0;

        // Checking the magic number (the first 4 bytes of the file)
        const char mag[] = MAGIC_NUMBER;
        if (!chunkNamesEq(mag, delta->buf)) {
                error("Error while reading delta: File did not start with 'DLTA'\n");
                return 0;
        }
        memcpy(&header->magicNumber, mag, 4);
        start += 4;

        // Getting the version if it starts with 'vrs' in the delta file
        union SerialDword ver = { .str = VERSION_CHUNK_NAME };
        union SerialDword bufVer = { .dword = 0 };
        ver.str[3] = 0;
        memcpy(bufVer.str, &delta->buf[start], 3);
        if (ver.dword != bufVer.dword) {
                error("Error while reading delta: Version was not labelled with 'vrs' or was not a version\n");
                return 0;
        }
        memcpy(header->versPseudoChunk, ver.str, 3);
        header->versionId = delta->buf[start + 3];
        start += 4;

        // Getting the target size
        littleDeserialize(delta->buf, delta->size, start, &header->targetSize);
        verbose("Got target length as %llu bytes\n", header->targetSize.longVal);
        start += 8;

        // Handling the other stuff. This also sets the data chunk size
        switch (header->versionId) {
        case 1:
                return readAndVerifyV1(args, header, delta, start);

        default:
                error("Error while reading delta: Version %d is not a valid version (min: 1, max: %d)\n",
                        header->versionId, CURRENT_VERSION);
                return 0;
        }
}

void diagnoseDeserializeError(enum CommandType type, uint8_t cmdSize, uint64_t i, 
        uint64_t read, uint64_t deltaSize) 
{
        error("Error while patching: ");
        if (cmdSize == GARBAGE_SERIAL_SIZE) {
                error("Command type '%c' is unknown\n", type);
        } else if (i + cmdSize >= deltaSize) {
                error("End of delta file was reached while deserializing '%c' command\n",
                        type);
        } else if (read == 0u) {
                // Should NEVER happen, but just in case
                error("Read the start of a command, even though it was out of bounds\n");
        } else {
                // Should NEVER happen, but just in case
                error("Unknown deserialization error\n");
        }
}

void diagnosePatchError(const struct Command *cmd, uint64_t outSize, 
        uint64_t srcSize) 
{
        error("Error while patching: ");
        switch (cmd->type) {
        case ADD_COMMAND:
                if (cmd->cmd.add.tgtIndex.longVal + patchSizeOf(cmd) > outSize) {
                        error("End of output was reached while patching an add command\n");
                } else {
                        error("Unknown move command error\n");
                }
                break;
        case MOVE_COMMAND:
                const struct MoveCommand *move = &cmd->cmd.move;
                if (move->srcIndex.longVal + patchSizeOf(cmd) > srcSize) {
                        error("End of source was reached while patching a move command\n",
                                cmd->type);
                } else if (move->tgtIndex.longVal + patchSizeOf(cmd) > outSize) {
                        error("End of output was reached while patching a move command\n");
                } else {
                        // An EOF error for the out buffer would have been caught
                        error("Unknown move command error\n");
                }
                break;
        default:
                // Should never happen, as the error should be caught at 
                // deserialization, not during patching.
                error("Command type 'c' is unknown\n", cmd->type);
                break;
        }
}

// Returns the maximum index set in outBuf, which is equivalent to the size
// of the target file. If an error occurred, GARBAGE_PATCH_SIZE is returned
uint64_t reconstructFromArgs(const struct Slurped *args, uint8_t *outBuf, 
        uint64_t outSize, const uint8_t *cmdBuf, uint64_t cmdBufSize)
{
        // Opening the source file
        struct FileBin *src = readBin(args->posArg1, args->posArg1Len);
        if (src == NULL) {
                // Message is printed in read bin; not important to even verbose
                return GARBAGE_PATCH_SIZE;
        }

        // TODO: Check the source's hash

        // Deserializing successive commands and applying them
        uint64_t i = 0;
        uint64_t tgtSize = 0;
        while (i < cmdBufSize) {
                struct Command cmd;

                // Deserializing the command
                const uint8_t read = deserializeCommand(cmdBuf, cmdBufSize, i, 
                        &cmd);
                const uint8_t cmdSize = serialSizeOf(&cmd);
                verbose(" \\___ Deserialized %llu-%llu; deserialized cmd '%c'\n", 
                        i, i + read, cmd.type);

                if (read != cmdSize) {
                        diagnoseDeserializeError(cmd.type, cmdSize, i, read, 
                                cmdBufSize);
                        freeBin(src);
                        return GARBAGE_PATCH_SIZE;
                }

                // // TODO: Version checking for commands
                // if (supportedVersion(cmd.type) > version) {
                //         loud("Warning: Command '%c' is supported in versions %d and above, but we're in version %d\n",
                //                 cmd.type, supportedVersion(cmd.type), version);
                //         // TODO: Warnings-as-errors flag
                // }

                // Applying the command
                const uint64_t patched = patchCommand(src->buf, src->size, 
                        outBuf, outSize, &cmd);
                verbose("   \\___ Patched %llu bytes for type '%c'\n", patched, 
                        cmd.type);
                if (patched != patchSizeOf(&cmd)) {
                        diagnosePatchError(&cmd, outSize, src->size);
                        freeBin(src);
                        return GARBAGE_PATCH_SIZE;
                }
                i += cmdSize;

                // Updating the calculated target size
                const uint64_t last = lastPatchedIndex(&cmd);
                tgtSize = last > tgtSize ? last : tgtSize;
        }

        // Cleaning up
        freeBin(src);
        return tgtSize;
}


// Returns EXIT_SUCCESS if the target was successfully reconstructed and 
// outputted; returns EXIT_FAILURE otherwise
int32_t reconstructTarget(struct Slurped *args) 
{
        // Getting our output file
        // This is done first to prevent postponing any errors or prompting
        // for a time when the user has already waited minutes (or hours)
        // for reconstruction to have finished
        FILE *outFile = attemptWFileOpen(args->outputFileName, args->outputLen, 
                args->flags);
        if (outFile == NULL) {
                verbose("Cancelled the reconstruction.\n");
                return EXIT_FAILURE;
        }

        // Opening our delta file
        struct FileBin *delta = readBin(args->posArg2, args->posArg2Len);
        if (delta == NULL) {
                verbose("Cancelled the reconstruction\n");
                return EXIT_FAILURE;
        }
        
        // Parsing the header to verify input files and get the parse parameters
        struct DeltaHeader header;
        const uint64_t dataStart = readAndVerifyHeader(args, &header, delta);
        debug("readAndVerifyHeader returned %llu\n", dataStart);
        if (dataStart == 0) {
                verbose("Cancelled the reconstruction\n");
                freeBin(delta);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        
        // Allocating our destination for reconstruction
        normal("Patching our commands to an output buffer... (takes a lot of time)\n");
        const uint64_t outSize = header.targetSize.longVal;
        debug("Allocating %llu bytes...\n", outSize);
        uint8_t *outBuf = (uint8_t *) malloc(outSize);
        debug("Allocated.\n");

        if (outBuf == NULL) {
                error("Error while patching: Could not allocate %llu byte output buffer\n",
                        outSize);
                freeBin(delta);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }

        // Reconstructing from the commands
        // We reassign the out size, as the actual size can be calculated (and 
        // the one from the delta file may be wrong)
        const uint64_t tgtSize = reconstructFromArgs(args, outBuf, outSize, 
                &delta->buf[dataStart], header.dataSize.longVal);
        if (tgtSize == GARBAGE_PATCH_SIZE) 
        {
                // Error message was already sent from serializeCmds()
                verbose("Cancelled reconstruction\n");
                freeBin(delta);
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        verbose("Got a new serial size of %llu bytes\n", tgtSize);
        freeBin(delta);

        if (tgtSize < outSize) {
                loud("Warning: The target size stored in the delta file is greater than the actual target size\n");
                // TODO: Warnings as errors
        }

        // Writing the serialized output to a file.
        normal("Writing the patched target buffer to a file...\n");
        if (fwrite(outBuf, 1, tgtSize, outFile) != tgtSize || ferror(outFile)) {
                error("Error while writing delta: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE; // TODO: Error code
        }

        // TODO: Verify hash of the reconstructed file.

        // Cleaning up
        normal("Finished outputing the delta\n");
        free(outBuf);
        fclose(outFile); // Doesn't really matter if this fails
        return EXIT_SUCCESS;
}