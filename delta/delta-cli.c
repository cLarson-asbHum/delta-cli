#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "delta.h"

// a million, or 'round-about there
#define LOOP_MAX 1000000
#define MAX_FILE_PATH_LEN 512
#define HELP_FLAG               (1u << (0))
#define VERSION_FLAG            (1u << (1))
#define DELTA_FLAG              (1u << (2))
#define RECONSTRUCT_FLAG        (1u << (3))
#define VERBOSE_FLAG            (1u << (4))
#define QUIET_FLAG              (1u << (5))
#define SILENT_FLAG             (1u << (6))
#define OUTPUT_FLAG             (1u << (7))
#define DESTINATION_DELETE_FLAG (1u << (8))
#define IGNORE_HASH_FLAG        (1u << (9))
#define ERROR_FLAG              (1u << (30))

int putFile(char *fileName)
{
        FILE *file = fopen(fileName, "r");

        if (file == NULL) {
                printf("\nCould not load the file (null at %s)", fileName);
                return EXIT_FAILURE;
        }

        int retCode = 0;
        for (int i = 0; (retCode != EOF) && (i < LOOP_MAX); i++) {
                retCode = fgetc(file);
                if (retCode != EOF) {
                        retCode = putchar(retCode /* Really a char */);
                }
        }

        if (retCode == EOF && ferror(file)) {
                printf("\nFailed the printing with error: %d", ferror(file));
                fclose(file);
                return EXIT_FAILURE;
        }

        fclose(file);
        return EXIT_SUCCESS;
}

struct Slurped {
        uint32_t flags;
        uint32_t outputLen;
        char *outputFileName;
        uint32_t posArg1Len;
        char *posArg1;
        uint32_t posArg2Len;
        char *posArg2;
};

// 0 if the strings are equal, including by length; a non-zero value otherwise
int streq(char *arg, char *exp, int maxCount) {
        if (strnlen(arg, maxCount) != strnlen(exp, maxCount)) {
                return -1;
        }

        return strncmp(arg, exp, maxCount);
}

// 1 if str ends with (or is equal to) ending. 0 otherwise
int endsWith(char *str, int strLen, char *ending, int endingLen) {
        if(strLen < endingLen) {
                return 0;
        }

        char *subStr = &(str[strLen - endingLen]);
        for(int i = 0; i < endingLen; i++) {
                printf("subStr[%d] == '%c'   ending[%d] == '%c'\n", i, subStr[i], 
                        i, ending[i]);
                if(subStr[i] != ending[i]) {
                        return 0;
                }
        } 

        return 1;
}

void slurpArgs(struct Slurped *out, int argc, char **argv) 
{
        out->flags = 0;
        out->outputLen = 0;
        out->posArg1Len = 0;
        out->posArg2Len = 0;

        if (argc == 1 || /* HOPEFULLY never happens: */ argc >= LOOP_MAX) {
                out->flags = (out->flags | HELP_FLAG);
                return;
        }

        int forceBreak = 0;
        int i = 1; 

        while ((!forceBreak) && (i < argc)) {
                char *arg = argv[i];
                printf("argv[%d]: \"%s\"\n", i, arg);
                int lookingForOutput = 0;

                // TODO: Optimize: compare in a hash map, and use a switch
                // ------------- COMMANDS ------------- 
                if (i == 1 && streq(arg, "delta", 6) == 0) {
                        out->flags = (out->flags | DELTA_FLAG);
                        i++;
                        continue;
                }
                
                if (i == 1 && streq(arg, "reconstruct", 12) == 0) {
                        out->flags = (out->flags | RECONSTRUCT_FLAG);
                        i++;
                        continue;
                }

                if(i == 1 && arg[0] != '-') {
                        // An option that wasn't "help" or "version" was arg 1
                        out->flags = (out->flags | ERROR_FLAG);
                        return; 
                }

                if (streq(arg, "-h", 3) == 0 || streq(arg, "--help", 7) == 0 
                        || streq(arg, "-?", 3) == 0 || streq(arg, "-help", 6) == 0) 
                {
                        out->flags = (out->flags | HELP_FLAG);
                        i++;
                        continue;
                }
                
                if (streq(arg, "-v", 3) == 0 || streq(arg, "--version", 10) == 0 
                        || streq(arg, "-version", 9) == 0) 
                {
                        out->flags = (out->flags | VERSION_FLAG);
                        i++;
                        continue;
                }
                
                if(arg[0] != '-') {
                        forceBreak = 1;
                        continue;
                }

                // ------------- OPTIONS ------------- 
                if (i > 1 && streq(arg, "--", 3) == 0) {
                        forceBreak = 1;
                        i++;
                        continue;
                }

                if (i > 1 && streq(arg, "--verbose", 10) == 0) {
                        // Override silent and quiet.
                        out->flags = (out->flags & ~(QUIET_FLAG | SILENT_FLAG));

                        // Apply the verbose flag
                        out->flags = (out->flags | VERBOSE_FLAG);
                        i++;
                        continue;
                }

                if (i > 1 && (streq(arg, "--quiet", 8) == 0 
                        || streq(arg, "--error", 8) == 0)) 
                {
                        // Override silent and verbose.
                        out->flags = (out->flags & ~(VERBOSE_FLAG | SILENT_FLAG));
                        
                        // Apply the quiet flag
                        out->flags = (out->flags | QUIET_FLAG);
                        i++;
                        continue;
                }

                if (i > 1 && streq(arg, "--silent", 9) == 0) {
                        // Override verbose and quiet.
                        out->flags = (out->flags & ~(QUIET_FLAG | VERBOSE_FLAG));
                        
                        // Apply the silent flag
                        out->flags = (out->flags | SILENT_FLAG);
                        i++;
                        continue;
                }

                if (i > 1 && streq(arg, "--force-destination-delete", 27) == 0) {
                        out->flags = (out->flags | DESTINATION_DELETE_FLAG);
                        i++;
                        continue;
                }
                
                if (i > 1 && streq(arg, "--ignore-hash", 14) == 0) {
                        out->flags = (out->flags | IGNORE_HASH_FLAG);
                        i++;
                        continue;
                }

                // TODO: Output flag can be repeated with no error
                // printf("Does compare with -o: %d", streq(arg, "-o", 3));
                if (i > 1 && i < argc - 1 && (streq(arg, "-o", 3) == 0 
                        || streq(arg, "--output", 9) == 0)) 
                {
                        out->flags = (out->flags | OUTPUT_FLAG);
                        out->outputLen = 0;
                        i++; 
                        arg = argv[i];
                        // TODO: validate output name
                        out->outputLen = strnlen(arg, MAX_FILE_PATH_LEN);

                        if(out->outputLen == 0) {
                                out->flags = (out->flags | ERROR_FLAG);
                                return;
                        }

                        out->outputFileName = arg;
                        i++;
                        continue;
                }

                // The argument was not recognized; error
                out->flags = (out->flags | ERROR_FLAG);
                return;
        }

        // Getting our positional args
        forceBreak = 0;
        if(i < argc) {
                char *arg = argv[i];
                out->posArg1Len = strnlen(arg, MAX_FILE_PATH_LEN);
                
                if(out->posArg1Len == 0) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return;
                }

                out->posArg1 = arg;
        } else {
                out->flags = (out->flags | ERROR_FLAG);
                return;
        }

        if(i + 1 < argc) {
                char *arg = argv[i + 1];
                out->posArg2Len = strnlen(arg, MAX_FILE_PATH_LEN);
                
                if(out->posArg2Len == 0) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return;
                }

                out->posArg2 = arg;
        } else {
                out->flags = (out->flags | ERROR_FLAG);
                return;
        }

        // Giving a default output
        if((out->flags & OUTPUT_FLAG) == 0 && (out->flags & RECONSTRUCT_FLAG)) {
                if(out->posArg2Len > 6 
                        && endsWith(out->posArg2, out->posArg2Len, ".delta", 6)) 
                {
                        // Removing a .delta extension
                        out->outputLen = out->posArg2Len - 6;
                        out->outputFileName = out->posArg2; // Always use strnlen
                } else {
                        // Adding a .reconstructed extension
                        // NOTE: This creates a memory leak, because we never free it,
                        //       however, the slurped args last till the end of the program
                        //       anyways, so we don't really care.
                        out->outputLen = out->posArg2Len + 14;
                        char *cat = malloc((out->outputLen + 1) * sizeof(char));
                        strncpy(cat, out->posArg2, out->posArg2Len + 1);
                        strncat(cat, ".reconstructed", 15);
                        out->outputFileName = cat;
                }

                return;
        }
        
        if((out->flags & OUTPUT_FLAG) == 0 && (out->flags & DELTA_FLAG)) {
                // NOTE: This creates a memory leak, because we never free it,
                //       however, the slurped args last till the end of the program
                //       anyways, so we don't really care.
                out->outputLen = out->posArg2Len + 6;
                char *cat = malloc((out->outputLen + 1) * sizeof(char));
                strncpy(cat, out->posArg2, out->posArg2Len + 1);
                strncat(cat, ".delta", 7);
                out->outputFileName = cat;
                return;
        }
}

int main(int argc, char **argv) 
{
        if (argc < 1) {
                exit(-1);
                return EXIT_FAILURE; // Unreachable
        }

        struct Slurped *slurpedPtr = malloc(sizeof(struct Slurped));
        slurpArgs(slurpedPtr, argc, argv);

        //#region DEV START: Showing how the args were slurped
        struct Slurped slurped = *slurpedPtr;
        printf("Flags: %08x\n", slurped.flags);

        printf("Output: \"");
        if(slurped.outputLen != 0) {
                printf("%s", slurped.outputFileName);
        }
        printf("\" (length %d)\n", slurped.outputLen);

        printf("Pos 1: \"");
        if(slurped.posArg1Len != 0) {
                printf("%s", slurped.posArg1);
        }
        printf("\" (length %d)\n", slurped.posArg1Len);
        
        printf("Pos 2: \"");
        if(slurped.posArg2Len != 0) {
                printf("%s", slurped.posArg2);
        }
        printf("\" (length %d)\n", slurped.posArg2Len);
        free(slurpedPtr);
        //#endregion DEV END
}