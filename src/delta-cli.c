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

int32_t displayHelp(uint32_t hasVerboseFlag) 
{
        char *home = getenv("DELTA_CLI_HOME");
        if (home == NULL) {
                error("Could not print help message.\n");
                detail(" \\___ %DELTA_CLI_HOME% environment variable could not be found");
                return EXIT_FAILURE;
        }

        const int32_t homeLen = strnlen(home, MAX_FILE_PATH_LEN) + 1;
        if (hasVerboseFlag) {
                const char helpFile[] = "/res/text/delta-help-long.txt";
                char *path = malloc(sizeof(helpFile) + sizeof(char) * homeLen);
                strncpy(path, home, sizeof(char) * homeLen);
                strncat(path, helpFile, sizeof(helpFile));
                const int32_t ret = putFile(path);
                free(path);
                return ret;
        } else {
                const char helpFile[] = "/res/text/delta-help-short.txt";
                char *path = malloc(sizeof(helpFile) + sizeof(char) * homeLen);
                strncpy(path, home, sizeof(char) * homeLen);
                strncat(path, helpFile, sizeof(helpFile));
                const int32_t ret = putFile(path);
                free(path);
                return ret;
        }
}

int32_t displayVersion() 
{
        return printf("delta-cli 1.1.0 | Connor Larson, 2026 | MIT license\n");
}


int32_t main(int32_t argc, char **argv) 
{
        // Parsing arguments if they're available
        if (argc < 1) {
                exit(EXIT_FAILURE);
                return EXIT_FAILURE; // Unreachable
        }

        loud("\n");
        struct Slurped *slurpedPtr = malloc(sizeof(struct Slurped));
        const enum SlurpErr slurpErr = slurpArgs(slurpedPtr, argc, argv);
        const uint32_t flags = slurpedPtr->flags;

        debug("Slurped Flags: %08x\n", flags);
        debug("Output Path: %s (length %llu)\n", slurpedPtr->outputFileName, slurpedPtr->outputLen);
        debug("Pos arg 1: %s (length %llu)\n", slurpedPtr->posArg1, slurpedPtr->posArg1Len);
        debug("Pos arg 2: %s (length %llu)\n", slurpedPtr->posArg2, slurpedPtr->posArg2Len);

        // Help and version flags take precedent over everything (including errors)
        if (flags & HELP_FLAG) {
                displayHelp(getLogLevel() >= VERBOSE_LVL);
                free(slurpedPtr);
                loud("\n");
                return EXIT_SUCCESS;
        }
        
        if (flags & VERSION_FLAG) {
                displayVersion();
                free(slurpedPtr);
                loud("\n");
                return EXIT_SUCCESS;
        }
        
        // Reporting an error and exiting if any occurred.
        setLogLevel(flagsToLogLevel(flags));
        debug("Log level was set to %d\n", getLogLevel());
        const int32_t argErr = displayErr(flags, argv, slurpErr, getSlurpIndex());
        if (argErr != SLURP_SUCCESS) {
                free(slurpedPtr);
                loud("\n");
                return argErr;
        }

        // Running the specified command
        if (flags & DELTA_FLAG) {
                const int32_t ret = computeDelta(slurpedPtr);
                free(slurpedPtr);
                loud("\n");
                return ret;
        }

        if (flags & RECONSTRUCT_FLAG) {
                const int32_t ret = reconstructTarget(slurpedPtr);
                free(slurpedPtr);
                loud("\n");
                return ret;
        }

        // Unreachable under normal operation
        error("Garbage state: No error occurred despite the command being unknown.\n");
        loud("\n");
        return EXIT_FAILURE;
}