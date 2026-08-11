#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "file-helper.h"
#include "delta-command.h"
#include "reconstruct-command.h"

// a million, or 'round-about there
#define LOOP_MAX 1000000

int displayHelp(uint32_t hasVerboseFlag) 
{
        char *home = getenv("DELTA_CLI_HOME");
        if (home == NULL) {
                error("Could not print help message.\n");
                normal(" \\___ %DELTA_CLI_HOME% environment variable could not be found");
                return EXIT_FAILURE;
        }

        const int homeLen = strnlen(home, MAX_FILE_PATH_LEN) + 1;
        if (hasVerboseFlag) {
                const char helpFile[] = "/res/text/delta-help-long.txt";
                char *path = malloc(sizeof(helpFile) + sizeof(char) * homeLen);
                strncpy(path, home, sizeof(char) * homeLen);
                strncat(path, helpFile, sizeof(helpFile));
                const int ret = putFile(path);
                free(path);
                return ret;
        } else {
                const char helpFile[] = "/res/text/delta-help-short.txt";
                char *path = malloc(sizeof(helpFile) + sizeof(char) * homeLen);
                strncpy(path, home, sizeof(char) * homeLen);
                strncat(path, helpFile, sizeof(helpFile));
                const int ret = putFile(path);
                free(path);
                return ret;
        }
}

int displayVersion() 
{
        printf("delta-cli 0.0.0 | Connor Larson, 2026 | MIT license\n");
}


int main(int argc, char **argv) 
{
        // Parsing arguments if they're available
        if (argc < 1) {
                exit(EXIT_FAILURE);
                return EXIT_FAILURE; // Unreachable
        }

        normal("\n");
        struct Slurped *slurpedPtr = malloc(sizeof(struct Slurped));
        const enum SlurpErr slurpErr = slurpArgs(slurpedPtr, argc, argv);
        const uint32_t flags = slurpedPtr->flags;

        debug("Slurped Flags: %08x\n", flags);
        debug("Output Path: %s (length %llu)\n", slurpedPtr->outputFileName, slurpedPtr->outputLen);
        debug("Pos arg 1: %s (length %llu)\n", slurpedPtr->posArg1, slurpedPtr->posArg1Len);
        debug("Pos arg 2: %s (length %llu)\n", slurpedPtr->posArg2, slurpedPtr->posArg2Len);

        // Help and version flags take precedent over everything (including errors)
        if (flags & HELP_FLAG) {
                displayHelp(flags & VERBOSE_FLAG);
                free(slurpedPtr);
                normal("\n");
                return EXIT_SUCCESS;
        }
        
        if (flags & VERSION_FLAG) {
                displayVersion();
                free(slurpedPtr);
                normal("\n");
                return EXIT_SUCCESS;
        }
        
        // Reporting an error and exiting if any occurred.
        setLogFlags(flags & (VERBOSE_FLAG | QUIET_FLAG | SILENT_FLAG));
        const int argErr = displayErr(flags, argv, slurpErr, getSlurpIndex());
        if (argErr != SLURP_SUCCESS) {
                free(slurpedPtr);
                normal("\n");
                return argErr;
        }

        // Running the specified command
        if (flags & DELTA_FLAG) {
                const int ret = computeDelta(slurpedPtr);
                free(slurpedPtr);
                normal("\n");
                return ret;
        }

        if (flags & RECONSTRUCT_FLAG) {
                const int ret = reconstructTarget(slurpedPtr);
                free(slurpedPtr);
                normal("\n");
                return ret;
        }

        // Unreachable under normal operation
        error("Garbage state: No error occurred despite the command being unknown.\n");
        normal("\n");
        return EXIT_FAILURE;
}