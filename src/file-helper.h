#ifndef FILE_HELPER_H
#define FILE_HELPER_H

#include <stdint.h>
#include <stdio.h>

struct FileBin {
        uint64_t size;
        uint8_t *buf;
};

// Displays all content of a file to the screen, using `printf`. This returns
// `EXIT_SUCCESS` if and only if the entire file was read successfully; 
// any other return is an error.
int32_t putFile(char *fileName);

// Attempts to open a read-only file, displaying a message if the file cannot 
// be opened. If the file cannot be opened, this returns NULL. The file must be 
// closed with `fclose()` after usage
FILE *attemptRFileOpen(char *filename, uint16_t maxCount); 

// Attempts to create a write-only file, displaying a message if the file cannot
// be opened. If the file cannot be opened, this returns NULL. If a file already 
// exists with the given filename, this deletes all content in that file. 
// 
// The returned file must be closed with `fclose()` after usage
FILE *createWFile(char *filename); 

uint64_t fileLength(FILE *file);

// Puts the entire contents of a read-only file into a byte buffer.
// This buffer MUST be freed after its used. This returns NULL if any error 
// occurs
struct FileBin *readBin(char *filename, uint16_t filenameLen);

void freeBin(struct FileBin *bin);

// Attempts to open or create a file for writing, displaying a message if the 
// file cannot be opened. The flags argument specifies whether to override
// an existing file, to prompt beforehand, or to only ever create new ones.
FILE *attemptWFileOpen(char *filename, uint16_t maxCount, uint32_t flags);

// Attempts to close the given file and then remove it. The file is not removed
// if either the file could not be closed, the preserve flag is set, or the 
// filename could not be found. This does NOT validate that the filename resolves
// to the specified file.
void closeMaybeRemove(FILE *toClose, const struct Slurped *args);

#endif