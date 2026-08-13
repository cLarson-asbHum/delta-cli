#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "log-level.h"
#include "slurp-arg.h"
#include "delta.h"

// a million, or 'round-about there
#define LOOP_MAX 1000000

uint32_t slurpIndex = 1;

uint32_t getSlurpIndex() {
        return slurpIndex;
}

// 1 if the strings are equal, including by length; a 0 otherwise
uint8_t streq(const char *arg, const char *exp, uint16_t maxCount) {
        if (strnlen(arg, maxCount) != strnlen(exp, maxCount)) {
                return 0;
        }

        return strncmp(arg, exp, maxCount) == 0;
}

// 1 if str ends with (or is equal to) ending. 0 otherwise
uint8_t endsWith(char *str, uint16_t strLen, char *ending, uint16_t endingLen) {
        if (strLen < endingLen) {
                return 0;
        }

        char *subStr = &(str[strLen - endingLen]);
        for (uint16_t i = 0; i < endingLen; i++) {
                debug("subStr[%d] == '%c'   ending[%d] == '%c'\n", i, subStr[i], 
                        i, ending[i]);
                if (subStr[i] != ending[i]) {
                        return 0;
                }
        } 

        return 1;
}

uint8_t minPosArgs(uint32_t flags) 
{
        if((flags & DELTA_FLAG) || (flags & RECONSTRUCT_FLAG)) {
                return 2;
        }

        // Unrecognized command, invalidate all arguments
        return 255;
}

uint8_t maxPosArgs(uint32_t flags) 
{
        if((flags & DELTA_FLAG) || (flags & RECONSTRUCT_FLAG)) {
                return 2;
        }

        // Unrecognized command; invalidate all arguments
        return 0;
}

enum SlurpErr getPosArgs(struct Slurped *out, uint8_t argc, char **argv, 
        uint32_t i) 
{
        // Handling errors with positional argument parsing
        // NOTE: Slurped only currently supports 2 arguments, but that's fine
        //       we are simply future proofing *this* code right now
        const uint8_t min = minPosArgs(out->flags);
        const uint8_t max = maxPosArgs(out->flags);
        if(argc - i > max) {
                out->flags = (out->flags | ERROR_FLAG);
                slurpIndex = i + max;
                return TOO_MANY_POS_ARGS;
        }

        if(argc - i < min) {
                out->flags = (out->flags | ERROR_FLAG);
                slurpIndex = argc - 1;
                return MISSING_POS_ARG;
        }

        // We have exactly as many positional arguments as we expect.
        // (^ We have yet to check that the pos args have *any* content in them)
        char *arg1 = argv[i];
        out->posArg1Len = strnlen(arg1, MAX_FILE_PATH_LEN);
        if (out->posArg1Len <= 0) {
                out->flags = (out->flags | ERROR_FLAG);
                return MISSING_POS_ARG;
        }
        out->posArg1 = arg1;

        char *arg2 = argv[i + 1];
        out->posArg2Len = strnlen(arg2, MAX_FILE_PATH_LEN);
        if (out->posArg2Len <= 0) {
                out->flags = (out->flags | ERROR_FLAG);
                return MISSING_POS_ARG;
        }
        out->posArg2 = arg2;

        return SLURP_SUCCESS;

}

enum SlurpErr defaultOutPath(struct Slurped *out) 
{
        if ((out->flags & OUTPUT_FLAG) == 0 && (out->flags & RECONSTRUCT_FLAG)) {
                if (out->posArg2Len > 6 
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

                return SLURP_SUCCESS;
        }
        
        if ((out->flags & OUTPUT_FLAG) == 0 && (out->flags & DELTA_FLAG)) {
                // NOTE: This creates a memory leak, because we never free it,
                //       however, the slurped args last till the end of the program
                //       anyways, so we don't really care.
                out->outputLen = out->posArg2Len + 6;
                char *cat = malloc((out->outputLen + 1) * sizeof(char));
                strncpy(cat, out->posArg2, out->posArg2Len + 1);
                strncat(cat, ".delta", 7);
                out->outputFileName = cat;
                return SLURP_SUCCESS;
        }

        return SLURP_SUCCESS;
}

enum SlurpErr checkHelpOrVers(struct Slurped *out, const char *arg) 
{
        if (streq(arg, "-h", 3) || streq(arg, "--help", 7) 
                || streq(arg, "-?", 3) || streq(arg, "-help", 6)) 
        {
                out->flags = (out->flags | HELP_FLAG);
                return SLURP_SUCCESS;
        }
        
        if (streq(arg, "-v", 3) || streq(arg, "--version", 10) 
                || streq(arg, "-version", 9)) 
        {
                out->flags = (out->flags | SHOW_VERSION_FLAG);
                return SLURP_SUCCESS;
        }

        return CONTINUE;
}

enum SlurpErr checkCmd(struct Slurped *out, const char *arg) 
{
        if (streq(arg, "delta", 6)) {
                out->flags = (out->flags | DELTA_FLAG);
                return SLURP_SUCCESS;
        }
        
        if (streq(arg, "reconstruct", 12)) {
                out->flags = (out->flags | RECONSTRUCT_FLAG);
                return SLURP_SUCCESS;
        }

        if (streq(arg, "garbage", 8)) {
                out->flags = (out->flags | GARBAGE_EASTER_EGG_FLAG | ERROR_FLAG);
                return UNKNOWN_COMMAND;
        }

        if (arg[0] != '-') {
                // a command that can't be "--help", "--version", or equivalent
                out->flags = (out->flags | ERROR_FLAG);
                return UNKNOWN_COMMAND; 
        }

        // Move to other checks to see if it's help or version
        return checkHelpOrVers(out, arg);
}

enum SlurpErr checkOpt(struct Slurped *out, int32_t argc, char **argv) 
{
        const uint32_t i = slurpIndex;
        const char *arg = argv[i];
        
        if (arg[0] != '-') {
                // This looks like a positional argument and not a flag;
                // thus, break out of the loop and parse positional arguments
                return FORCE_BREAK;
        }

        if (streq(arg, "--", 3)) {
                // Break out of the loop and parse positional arguments
                slurpIndex++;
                return FORCE_BREAK;
        }

        if (streq(arg, "--verbose", 10)) {
                // Override silent and quiet, which conflict with verbose
                out->flags = (out->flags & ~(QUIET_FLAG | SILENT_FLAG));
                out->flags = (out->flags | VERBOSE_FLAG);
                return SLURP_SUCCESS;
        }

        if (streq(arg, "--quiet", 8) || streq(arg, "--error", 8)) {
                // Override silent and verbose, which conflict with quiet
                out->flags = (out->flags & ~(VERBOSE_FLAG | SILENT_FLAG));
                out->flags = (out->flags | QUIET_FLAG);
                return SLURP_SUCCESS;
        }

        if (streq(arg, "--silent", 9)) {
                // Override verbose and quiet, which conflict with silent
                out->flags = (out->flags & ~(QUIET_FLAG | VERBOSE_FLAG));
                out->flags = (out->flags | SILENT_FLAG);
                return SLURP_SUCCESS;
        }
        
        if (streq(arg, "-p", 3) || streq(arg, "--prompt", 9)) {
                // Override the force delete flag, which conflicts with prompt
                out->flags = (out->flags & ~(DESTINATION_DELETE_FLAG));
                out->flags = (out->flags | PROMPT_FLAG);
                return SLURP_SUCCESS;
        }

        if (streq(arg, "--force-destination-delete", 27)) {
                // Adding the destination delete flag if prompt isn't present
                if(!(out->flags & PROMPT_FLAG)) {
                        out->flags = (out->flags | DESTINATION_DELETE_FLAG);
                }
                return SLURP_SUCCESS;
        }
        
        if (streq(arg, "--ignore-hash", 14)) {
                out->flags = (out->flags | IGNORE_HASH_FLAG);
                return SLURP_SUCCESS;
        }

        if (streq(arg, "-o", 3) || streq(arg, "--output", 9)) {
                if (i >= argc - 1) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return ZERO_LENGTH_OUTPUT_PATH;
                }
                
                out->flags = (out->flags | OUTPUT_FLAG);
                out->outputLen = 0;
                
                // Going to the next argument and treat that as our out path
                // TODO: validate output path
                slurpIndex = i + 1; 
                char *nextArg = argv[slurpIndex];
                out->outputLen = strnlen(nextArg, MAX_FILE_PATH_LEN);

                if (out->outputLen == 0) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return ZERO_LENGTH_OUTPUT_PATH;
                }

                out->outputFileName = nextArg;
                return SLURP_SUCCESS;
        }

        if (streq(arg, "--preserve", 11)) {
                out->flags = (out->flags | PRESERVE_FLAG);
                return SLURP_SUCCESS;
        }
        
        // TODO: support for two-digit versions
        if (streq(arg, "--file-version=", 15)) {
                const int32_t len = strnlen(arg, 17);
                debug(" \\___ File version-like length is %d\n", len);
                if (len != 16 || !isdigit(arg[15])) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return MALFORMED_FILE_VERSION;
                }
                out->flags = (out->flags | FILE_VERSION_FLAG);

                // NOTE: Assumes the digit is encoded with ASCII/UTF-8
                out->version = ((int16_t) arg[15]) - (int16_t) '0';
                debug(" \\___ Parsed version as %d\n", out->version);
                if (out->version < 1 || out->version > CURRENT_VERSION) {
                        out->flags = (out->flags | ERROR_FLAG);
                        return UNKNOWN_FILE_VERSION;
                }
                return SLURP_SUCCESS;
        }

        return checkHelpOrVers(out, arg);
}

enum SlurpErr handleArg(struct Slurped *out, int32_t argc, char **argv) 
{
        const uint32_t i = slurpIndex;
        const char *arg = argv[i];
        
        // TODO: Optimize: compare in a hash map, and use a switch
        // If this is the first argument, check if it is 
        enum SlurpErr cmdRet = CONTINUE;
        if (i == 1) {
                cmdRet = checkCmd(out, arg);
                debug(" \\___ Parsed cmd arg <%d> that returned <%d>\n", i, cmdRet);
        }

        if(cmdRet != CONTINUE) {
                return cmdRet;
        }

        // ------------- OPTIONS ------------- 
        enum SlurpErr optRet = CONTINUE;
        if (i > 1) {
                optRet = checkOpt(out, argc, argv);
                debug(" \\___ Parsed opt arg <%d> that returned <%d>\n", i, optRet);
        }

        if(optRet != CONTINUE) {
                return optRet;
        }
                
        // The argument was not recognized; error
        out->flags = (out->flags | ERROR_FLAG);
        return UNKNOWN_FLAG_OR_ARG;
}

enum SlurpErr slurpArgs(struct Slurped *out, int32_t argc, char **argv) 
{
        out->flags = 0;
        out->outputLen = 0;
        out->posArg1Len = 0;
        out->posArg2Len = 0;

        if (argc == 1 || argc >= LOOP_MAX) {
                out->flags = (out->flags | HELP_FLAG);
                return SLURP_SUCCESS;
        }

        uint8_t forceBreak = 0;
        slurpIndex = 1;

        while (!forceBreak && slurpIndex < argc) {
                debug("argv[%d]: \"%s\"\n", slurpIndex, argv[slurpIndex]);

                const enum SlurpErr ret = handleArg(out, argc, argv);
                if (ret == FORCE_BREAK) {
                        forceBreak = 1;
                        continue; // Breaks out of the loop
                }

                if (ret != SLURP_SUCCESS && ret != FORCE_BREAK) {
                        return ret;
                }

                // By default, SLURP_SUCCESS just goes to the next argument.
                // handleArg() may or may not have mutated slurpIndex (e.g. 
                // --output incremented to the next argument).
                slurpIndex++;
        }

        // No need to parse positional arguments for help and version
        // This allows these commands to, as the docs say, ignore everything else.
        if ((out->flags & HELP_FLAG) || (out->flags & SHOW_VERSION_FLAG)) {
                return SLURP_SUCCESS;
        }

        // Getting our positional args
        const enum SlurpErr posArgRet = getPosArgs(out, argc, argv, slurpIndex);
        if (posArgRet != SLURP_SUCCESS) {
                return posArgRet;
        }
        
        // Giving a default output path
        return defaultOutPath(out);
}

// Displays a message for the slurpErr variable. Returns SLURP_SUCCESS if there 
// was no error to begin with
enum SlurpErr displayErr(uint32_t flags, char **argv, enum SlurpErr err, 
        uint32_t i) 
{
        const uint32_t hasFlag = (flags & ERROR_FLAG);

        if (hasFlag && err == SLURP_SUCCESS) {
                error("Garbage program state: success, but error flag was set.\n");
                return GARBAGE_SUCCESS_WITH_ERR_FLAG;
        }
        
        if (!hasFlag && err != SLURP_SUCCESS) {
                error("Garbage program state: err with code <%d>, but error flag was not set.\n",
                        err);
                return GARBAGE_ERR_WITHOUT_ERR_FLAG;
        }

        if (err == SLURP_SUCCESS) {
                return SLURP_SUCCESS;
        }

        // Determining *why* the error flag is set
        error("Error at argument %d: ", i);
        switch(err) {
        case UNKNOWN_COMMAND: 
                error("\"%s\" is not a valid command\n", argv[i]);
                
                if (flags & GARBAGE_EASTER_EGG_FLAG) {
                        normal(" \\___ Very funny... At least you read the docs, i guess?\n");
                }
                break;

        case UNKNOWN_FLAG_OR_ARG: 
                error("\"%s\" is not a valid flag or argument\n", argv[i]);
                normal(" \\___ If a file starts with '-', try adding the '--' flag before it");
                break;

        case ZERO_LENGTH_OUTPUT_PATH: 
                error("Output path (from -o or --output) had 0 length\n");
                break;

        case MISSING_POS_ARG: 
                error("Missing positional argument (e.g. src.txt or target.delta)\n");
                normal(" \\___ If one starts with '-', try adding the '--' flag before it");
                break;

        case TOO_MANY_POS_ARGS:
                error("Too many positional arguments (expected only %d args)\n",
                        maxPosArgs(flags));
                normal(" \\___ Check that all optional arguments have a flag before them\n");
                verbose("   \\___ e.g. \"--option path/file.ex\" rather than just \"path/file.ex\"\n");
                normal(" \\___ Verify that all options have '-' or '--' before them\n");
                verbose("   \\__ e.g. \"--prompt\" or \"-p\", rather than just \"prompt\"\n");
                break;

        case MALFORMED_FILE_VERSION:
                error("File version must be only a single digit\n");
                break;
                
        case UNKNOWN_FILE_VERSION:
                error("File version must be between 1 and %d\n", CURRENT_VERSION);
                break;

        case FORCE_BREAK:
        case CONTINUE:
                error("Garbage error <%d> was returned from argument slurp\n",
                        err);
                break;

        default:
                error("Error with unknown code <%d>\n", err);
                break;
        }

        return err;
}
