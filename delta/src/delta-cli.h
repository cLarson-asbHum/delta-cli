#ifndef DELTA_CLI_H
#define DELTA_CLI_H

#include <stdio.h>
#include <stdint.h>

// Prints the format if and only if the DEBUG macro is 1.
void debug(const char *format, ...);

// Prints the format to stdout if and only if verbose mode is enabled
int verbose(const char *format, ...);

// Prints the format to stdout if neither quiet nor silent mode are enabled
int normal(const char *format, ...);

// Prints the format to stdout if and only if silent is not enabled. 
int loud(const char *format, ...);

// Prints the format to stderr if and only if silent is not enabled. 
int error(const char *format, ...);

struct FileBin {
        uint64_t size;
        uint8_t *buf;
};

// Attempts to open or create a file for writing, displaying a message if the 
// file cannot be opened. The flags argument specifies whether to override
// an existing file, to prompt beforehand, or to only ever create new ones.
FILE *attemptWFileOpen(char *filename, int maxCount, uint32_t flags);

void closeMaybeRemove(FILE *toClose, const struct Slurped *args);

// Puts the entire contents of a read-only file into a byte buffer.
// This buffer MUST be freed after its used. This returns NULL if any error 
// occurs
struct FileBin *readBin(char *filename, int filenameLen);

void freeBin(struct FileBin *bin);

#endif
