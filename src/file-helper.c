
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <io.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "file-helper.h"

#define LOOP_MAX 1000000

int32_t putFile(char *fileName)
{
        FILE *file = fopen(fileName, "r");

        if (file == NULL) {
                error("Error while displaying: Could not load the file (null at %s)", 
                        fileName);
                return EXIT_FAILURE;
        }

        uint32_t retCode = 0;
        for (uint32_t i = 0; (retCode != EOF) && (i < LOOP_MAX); i++) {
                retCode = fgetc(file);
                if (retCode != EOF) {
                        retCode = putchar(retCode);
                }
        }

        if (retCode == EOF && ferror(file)) {
                error("Error while displaying: Standard error code <%d>", 
                        ferror(file));
                fclose(file);
                return EXIT_FAILURE;
        }

        fclose(file);
        return EXIT_SUCCESS;
}

FILE *attemptRFileOpen(const char *filename, uint16_t maxCount) 
{
        // Getting the file name 
        char *subName = malloc(sizeof(char) * (maxCount + (uint16_t) 1u));
        strncpy(subName, filename, maxCount);
        subName[maxCount] = '\0';

        // Opening the file
        FILE *file = fopen(subName, "rb"); // b to Win means that this is binary

        if (file == NULL) {
                error("Cannot open file \"%s\" for reading\n", subName);
                normal(" \\___ Check the spelling of the path and the file name \n");
                normal(" \\___ If the file exists, ensure it's not being used in another program \n");
        } else {
                verbose("Opened file \"%s\" as read-only\n", subName);
        }

        // Cleaning up
        free(subName);
        return file;
}

FILE *createWFile(char *filename) 
{
        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
                error("Error while creating file: Could not create file \"s\" for writing\n",
                        filename);
                return NULL;
        }
        verbose("Created file \"%s\"\n", filename);
        return file;
}

uint64_t fileLength(FILE *file) 
{
        return _filelength(_fileno(file));
}

struct FileBin *readBin(char *filename, uint16_t filenameLen)
{
        // NOTE: This uses the Win _fileno() function to create a buffer
        // Opening the src file and reading from
        FILE *src = attemptRFileOpen(filename, filenameLen);
        if (src == NULL) {
                return NULL;
        }

        const uint64_t srcSize = fileLength(src);
        if (srcSize <= 0) {
                error("Error while reading from file: Source file length was 0 or an error.\n");
                // TODO: Err code  should be set somewhere
                return NULL;
        }
        uint8_t *srcBuf = malloc(srcSize);
        if (srcBuf == NULL) {
                error("Error while reading from file: Failed to allocate %llu bytes\n", 
                        srcSize);
                free(srcBuf);
                // TODO: Failure should be set somewhere
                return NULL;
        }

        normal("Reading from \"%s\"... (this may take awhile)\n", filename);
        const uint64_t srcRead = fread((void *) srcBuf, 1, srcSize, src);
        if (srcRead != srcSize) {
                error("Error while reading from file: Expected to read %llu bytes; read %llu\n", 
                        srcSize, srcRead);
                normal(" \\___ Reason: %s\n", strerror(errno));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        if (ferror(src)) {
                error("Error while reading from file: %s\n", strerror(ferror(src)));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        fclose(src);

        if (ferror(src)) {
                error("Error while reading from file: %s\n", strerror(ferror(src)));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        struct FileBin *result = malloc(sizeof(struct FileBin));
        result->size = srcSize;
        result->buf = srcBuf;
        return result;
}

void freeBin(struct FileBin *bin) {
        if (bin == NULL) {
                return;
        }

        free(bin->buf);
        free(bin);
}

// Returns 1 if the existing file should be overridden; 0 otherwise. This 
// prints messages to stdout (and/or stderr), and gets input from stdin
// if the prompt flag is set
uint8_t overrideExisting(const char* subName, uint32_t flags) 
{
        const uint32_t force = flags & DESTINATION_DELETE_FLAG;
        const uint32_t prompt = (flags & PROMPT_FLAG) 
                && !(flags & SILENT_FLAG);
        
        char userOpinion = 'n';
        if (prompt) {
                // Get the user's opinion from stdin
                // This always occurs with the prompt flag, even if force is set
                loud("Would you like to override all content in \"%s\"? \n",
                        subName);
                loud("y/n (default is 'n') > ");
                int32_t resp = getchar();
                debug("Response was 0x%08x \n", resp);
                if (resp == EOF || ferror(stdin)) {
                        loud("Warning: user input had an error (defaulting to 'n')\n");
                }
                
                if(resp == 'y') {
                        userOpinion = 'y';
                }
        }

        if (prompt && userOpinion != 'y') {
                // User says to NOT delete the file
                // Respect their opinion (and don't inform them that they can 
                // override this behavior)
                normal(" \\___ Cancelled.\n");
                return 0;
        }

        if (!prompt && !force) {
                // No flags were set, so take the safest option.
                // Unlike if the user opinionates 'n', we inform them
                // that this behavior can be overridden with flags
                // The user's opinion is to not delete or there were no flags,
                // so we exit the program.
                error("Error while creating file: File \"%s\" already exists\n",
                        subName);

                if (!prompt) {
                        normal(" \\___ To ask to override the file, use the -p or --prompt command flag\n");
                }
                return 0;
        }

        if ((!prompt && force) || (prompt && userOpinion == 'y')) {
                // We were only ever told to override the file, so do.
                return 1u;
        }

        // Should be unreachable, but just in case, assume 'n'
        return 0u;
}

// Attempts to open or create a file for writing, displaying a message if the 
// file cannot be opened. The flags argument specifies whether to override
// an existing file, to prompt beforehand, or to only ever create new ones.
FILE *attemptWFileOpen(char *filename, uint16_t maxCount, uint32_t flags) 
{
        // Getting the file name 
        char *subName = malloc(sizeof(char) * (maxCount + 1uLL));
        strncpy(subName, filename, maxCount);
        subName[maxCount] = '\0';

        // Opening the file in read-only mode to check if it exists
        FILE *file = fopen(subName, "rb"); // b to Win means that this is binary

        if (file == NULL) {
                // The file did not exist; create one for writing
                file = createWFile(subName);
                free(subName);
                return file;
        }

        // The file DID exist; use the flags to figure out what to do
        if (fclose(file) != 0) {
                error("Error while creating file: Could not close \"%s\"\n", 
                        subName);
                free(subName);
                return NULL;
        }

        if (overrideExisting(subName, flags)) {
                // Only told to force delete, so do it
                file = createWFile(subName);
                normal(" \\___ Previous contents of the file were overridden\n");
        } else {
                file = NULL;
        }

        free(subName);
        return file;
}

// Attempts to close the given file and then remove it. The file is not removed
// if either the file could not be closed, the preserve flag is set, or the 
// filename could not be found. This does NOT validate that the filename resolves
// to the specified file.
void closeMaybeRemove(FILE *toClose, const struct Slurped *args)
{
        if (fclose(toClose) != 0)  {
                loud("Warning: file could not be closed on error\n");
                return;
        }
        
        if(args->flags & PRESERVE_FLAG) {
                return;
        }

        // Getting the file name 
        const uint16_t len = args->outputLen;
        const char *name = args->outputFileName;
        char *subName = malloc(sizeof(char) * (len + 1u));
        strncpy(subName, name, len);
        subName[len] = '\0';

        // Removing the file
        remove(subName);
        free(subName);
}

