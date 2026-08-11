#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "slurp-arg.h"
#include "log-level.h"
#include "file-helper.h"
#include "hash.h"
#include "delta.h"

// Returns the index at which commands start, i.e. the index after 
// DeltaHeader.dataSize. If an error occurs, this returns 0; this shouldn't be 
// misinterpreted as an actual index, as a delta file MUST have at least 
// 'DLTA' at the start, which guarantees that index 0 is NOT a command.
uint64_t readAndVerifyV1(const struct Slurped *args, struct DeltaHeader *header, 
        const struct FileBin *delta, uint64_t start)
{
        if (delta->size < V1_DELTA_HEADER_SIZE) {
                error("Error while reading delta: Delta file must have an incomplete header (size < %d bytes)\n",
                        V1_DELTA_HEADER_SIZE);
                normal(" \\___ A delta generated with the --preserve flag may be incomplete because of an error\n");
                return 0;
        }

        struct Version1Header *v1 = &header->header.v1;
        union SerialDword {
                char chars[4];
                uint32_t dword;
        };

        // Searching for the meta chunk.
        // If there is a chunk we don't recognize, we skip it and check the next
        const union SerialDword met = { .chars = META_CHUNK_NAME };
        uint8_t isMeta = 0;
        #define SEARCH_MAX ((uint64_t) 0x0000000f00000000ull)

        do {
                // Comparing the chunk name against the meta chunk name ("meta")
                union SerialDword curName;
                verbose("Byte %d: Checking whether '", start);
                for (int i = 0; i < 4; i++) {
                        curName.chars[i] = delta->buf[start + i];
                        verbose("%c", curName.chars[i]);
                }
                verbose("' is 'meta'\n");
                isMeta = (curName.dword == met.dword);
                start += 4;

                // Getting the chunk size
                // We store in meta size as it is most convenient
                if (littleDeserialize(delta->buf, delta->size, start,
                        &v1->metaSize) != 8) 
                {
                        // This should NEVER happen, but we check just in case
                        error("Error while reading delta: Attempted to read a chunk's size even though it was out of bounds\n");
                        return 0;
                }
                debug("Byte %d: Parsed the chunk size as %d\n", start, 
                        v1->metaSize.longVal);
                start += 8;

                // Skipping this current chunk if it isn't the meta chunk
                if(!isMeta) {
                        const char *c = curName.chars;
                        loud("Warning: Encountered unexpected chunk \"%c%c%c%c\". It has been skipped\n",
                                c[0], c[1], c[2], c[3]);
                        verbose("Byte %d: Not a meta chunk; skipping %d bytes\n",
                                start, v1->metaSize.longVal);
                        start += v1->metaSize.longVal;
                }
        } while (!isMeta && start + 11 < delta->size && start < SEARCH_MAX);

        if (!isMeta) {
                error("Error while reading delta: Could not find v1 meta chunk.\n");
                return 0;
        } 

        if(start + v1->metaSize.longVal > delta->size) {
                error("Error while reading delta: Meta chunk size (%d bytes) extends out of bounds\n",
                        v1->metaSize.longVal);
                return 0;
        }
        
        if (v1->metaSize.longVal < V1_META_SIZE) {
                error("Error while reading delta: Meta chunk size is not large enough (%d bytes < %d bytes)\n",
                        v1->metaSize.longVal, V1_META_SIZE);
                return 0;
        }

        // Getting the source and target hashes
        for (int i = 0; i < 32; i++) {
                v1->sourceHash.bytes[i] = delta->buf[start + i];
                v1->targetHash.bytes[i] = delta->buf[start + i + 32];
        }
        start += 64;

        // Verifying the source hash
        uint8_t ignoreHash = 1 || (args->flags & IGNORE_HASH_FLAG);
        
        // // TODO: Check if the hashes are NULL_SHA
        // if (v1->sourceHash.bytes[]) {
        //         loud("Warning: No source hash was provided");
        //         verbose(" \\___ More accurately")
        //         ignoreHash = 1;
        // }

        // FIXME: Because we can't currently hash, we just assume --ignore-hash
        if (ignoreHash) {
                loud("Warning: Ignoring hash verification for source files\n");
                normal(" \\___ The reconstructed target could possibly be silently corrupted as a result\n");
                // TODO: Warnings-as-errors flag
        }

        // We ignore the cmdLensEqual member, as using it would increase the 
        // complexity of the program (even though it would increase speed)
        start++;

        // Skipping the padding bytes
        start += 3;

        // Searching for the data chunk.
        // If there is a chunk we don't recognize, we skip it and check the next
        const union SerialDword dat = { .chars = DATA_CHUNK_NAME };
        uint8_t isData = 0;

        do {
                // Comparing the chunk name against the data chunk name ("data")
                union SerialDword curName;
                verbose("Byte %d: Checking whether '", start);
                for (int i = 0; i < 4; i++) {
                        curName.chars[i] = delta->buf[start + i];
                        verbose("%c", curName.chars[i]);
                }
                verbose("' is 'data'\n");
                isData = (curName.dword == dat.dword);
                start += 4;

                // Getting the chunk size
                // We store in data size as it is most convenient
                if (littleDeserialize(delta->buf, delta->size, start,
                        &header->dataSize) != 8) 
                {
                        // This should NEVER happen, but we check just in case
                        error("Error while reading delta: Attempted to read a chunk's size even though it was out of bounds\n");
                        return 0;
                }
                debug("Byte %d: Parsed the chunk size as %d\n", start,
                        header->dataSize.longVal);
                start += 8;

                // Skipping this current chunk if it isn't the data chunk
                if(!isData) {
                        const char* c = curName.chars;
                        loud("Warning: Encountered unexpected chunk \"%c%c%c%c\". It has been skipped\n",
                                c[0], c[1], c[2], c[3]);
                        verbose("Byte %d: Not a data chunk; skipping %d bytes\n",
                                start, header->dataSize.longVal);
                        start += header->dataSize.longVal;
                }
        } while (!isData && start + 11 < delta->size && start < SEARCH_MAX);

        if (!isData) {
                error("Error while reading delta: Could not find v1 data chunk.\n");
                return 0;
        } 

        if(start + header->dataSize.longVal > delta->size) {
                error("Error while reading delta: Data chunk size (%d bytes) extends out of bounds\n",
                        header->dataSize.longVal);
                return 0;
        }
        
        if (header->dataSize.longVal < 1) {
                error("Error while reading delta: Data chunk size is not large enough (%d bytes < %d bytes)\n",
                        header->dataSize.longVal, 1);
                return 0;
        }

        return start;
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
        if (delta->size < 16) {
                error("Error while reading delta: File was too small (< %d bytes)\n",
                        16);
                return 0;
        }

        uint64_t start = 0;

        // Checking the magic numbers
        const char mag[] = MAGIC_NUMBER;
        for (int i = 0; i < 4; i++) {
                verbose("Byte at %d: 0x%02x (expecting 0x%02x)\n", i, 
                        delta->buf[start], mag[i]);
                if (delta->buf[start] != mag[i]) {
                        error("Error while reading delta: File did not start with 'DLTA'\n");
                        return 0;
                }
                header->magicNumber[i] = mag[i];
                start++;
        }

        // Getting the version
        const char ver[] = VERSION_CHUNK_NAME;
        for (int i = 0; i < 3; i++) {
                verbose("Byte at %d: 0x%02x (expecting 0x%02x)\n", i, 
                        delta->buf[start], ver[i]);
                if (delta->buf[start] != ver[i]) {
                        error("Error while reading delta: Version was not labelled with 'vrs' or was not a version\n");
                        return 0;
                }
                header->versPseudoChunk[i] = ver[i];
                start++;
        }
        header->versionId = delta->buf[start];
        start++;

        // Getting the target size
        littleDeserialize(delta->buf, delta->size, start, &header->targetSize);
        verbose("Got target length as %d bytes\n", header->targetSize.longVal);
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

void diagnoseDeserializeError(enum CommandType type, uint64_t cmdSize, uint64_t i, 
        uint64_t read, uint64_t srcSize) 
{
        error("Error while patching: ");
        if (cmdSize == -1) {
                error("Command type '%c' is unknown\n", type);
        } else if (i + cmdSize > srcSize) {
                error("End of file was reached while deserializing '%c' command\n",
                        type);
        } else if (read == 0) {
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

// Returns EXIT_FAILURE on error; EXIT_SUCCESS if and only if on success
uint32_t reconstructFromArgs(const struct Slurped *args, uint8_t *outBuf, 
        uint64_t outSize, const uint8_t *cmdBuf, uint64_t cmdBufSize)
{
        // Opening the source file
        struct FileBin *src = readBin(args->posArg1, args->posArg1Len);
        if (src == NULL) {
                // Message is printed in read bin; not important to even verbose
                return EXIT_FAILURE;
        }

        // Deserializing successive commands and applying them
        uint64_t i = 0;
        while (i < cmdBufSize) {
                struct Command cmd;

                // Deserializing the command
                const int read = deserializeCommand(cmdBuf, cmdBufSize, i, &cmd);
                const int cmdSize = serialSizeOf(&cmd);
                verbose(" \\___ Deserialized %d-%d; deserialized cmd '%c'\n", 
                        i, i + read, cmd.type);

                if (read != cmdSize) {
                        diagnoseDeserializeError(cmd.type, cmdSize, i, read, 
                                src->size);
                        freeBin(src);
                        return EXIT_FAILURE;
                }

                // // TODO: Version checking for commands
                // if (supportedVersion(cmd.type) > version) {
                //         loud("Warning: Command '%c' is supported in versions %d and above, but we're in version %d\n",
                //                 cmd.type, supportedVersion(cmd.type), version);
                //         // TODO: Warnings-as-errors flag
                // }

                // Applying the command
                // FIXME: patchCommand should return a uint64_t, as the bounds of a command are LARGE
                const int patched = patchCommand(src->buf, src->size, outBuf, 
                        outSize, &cmd);
                verbose("   \\___ Patched %d bytes for type '%c'\n", patched, 
                        cmd.type);
                // FIXME: patchSizeOf should return a uint64_t, as the bounds of a command are LARGE
                if (patched != patchSizeOf(&cmd)) {
                        diagnosePatchError(&cmd, outSize, src->size);
                        freeBin(src);
                        return EXIT_FAILURE;
                }
                i += cmdSize;
        }

        // Cleaning up
        freeBin(src);
        return EXIT_SUCCESS;
}


int reconstructTarget(struct Slurped *args) 
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
        debug("readAndVerifyHeader returned %d\n", dataStart);
        if (dataStart == 0) {
                verbose("Cancelled the reconstruction\n");
                freeBin(delta);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        
        // Allocating our destination for reconstruction
        normal("Patching our commands to an output buffer... (takes a lot of time)\n");
        const uint64_t outSize = header.targetSize.longVal;
        debug("Allocating %d bytes...\n", outSize);
        uint8_t *outBuf = (uint8_t *) malloc(outSize);
        debug("Allocated.\n");

        if (outBuf == NULL) {
                error("Error while patching: Could not allocate %d byte output buffer\n",
                        outSize);
                freeBin(delta);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }

        // Reconstructing from the commands
        if (reconstructFromArgs(args, outBuf, outSize, &delta->buf[dataStart], 
                delta->size - dataStart) != EXIT_SUCCESS) 
        {
                // Error message was already sent from serializeCmds()
                verbose("Cancelled reconstruction\n");
                freeBin(delta);
                free(outBuf);
                closeMaybeRemove(outFile, args);
                return EXIT_FAILURE;
        }
        freeBin(delta);

        // Writing the serialized output to a file.
        normal("Writing the patched target buffer to a file...\n");
        const int written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize || ferror(outFile)) {
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