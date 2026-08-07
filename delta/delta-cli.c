#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include "delta.h"

// a million, or 'round-about there
#define DEBUG 0
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
#define GARBAGE_EASTER_EGG_FLAG (1u << (10))
#define PROMPT_FLAG             (1u << (11))
#define ERROR_FLAG              (1u << (30))

uint32_t logFlags = 0; 

// Prints the format if and only if the DEBUG macro is 1.
void debug(const char *format, ...) {
        // Hopefully the compiler can optimize enough to remove this.
        if (DEBUG) {
                va_list args;
                va_start(args, format);
                const int ret = vfprintf(stdout, format, args);
                va_end(args);
        }
}

// Prints the format to stdout if and only if verbose mode is enabled
int verbose(const char *format, ...) {
        if ((logFlags & VERBOSE_FLAG)) {
                va_list args;
                va_start(args, format);
                const int ret = vfprintf(stdout, format, args);
                va_end(args);
                return ret;
        }

        return 0;
}

// Prints the format to stdout if neither quiet nor silent mode are enabled
int normal(const char *format, ...) {
        if ((logFlags & QUIET_FLAG) || (logFlags & SILENT_FLAG)) {
               return 0; 
        }

        va_list args;
        va_start(args, format);
        const int ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

// Prints the format to stdout if and only if silent is not enabled. 
int loud(const char *format, ...) {
        // TODO: Reduced logging mode (between normal and error)
        if (logFlags & SILENT_FLAG) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

// Prints the format to stderr if and only if silent is not enabled. 
int error(const char *format, ...) {
        if (logFlags & SILENT_FLAG) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int ret = vfprintf(stderr, format, args);
        va_end(args);
        return ret;
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

// 1 if the strings are equal, including by length; a 0 otherwise
int streq(const char *arg, const char *exp, int maxCount) {
        if (strnlen(arg, maxCount) != strnlen(exp, maxCount)) {
                return 0;
        }

        return strncmp(arg, exp, maxCount) == 0;
}

// 1 if str ends with (or is equal to) ending. 0 otherwise
int endsWith(char *str, int strLen, char *ending, int endingLen) {
        if (strLen < endingLen) {
                return 0;
        }

        char *subStr = &(str[strLen - endingLen]);
        for (int i = 0; i < endingLen; i++) {
                debug("subStr[%d] == '%c'   ending[%d] == '%c'\n", i, subStr[i], 
                        i, ending[i]);
                if (subStr[i] != ending[i]) {
                        return 0;
                }
        } 

        return 1;
}

enum SlurpErr {
        SLURP_SUCCESS = 0,
        
        UNKNOWN_COMMAND         = -1,
        UNKNOWN_FLAG_OR_ARG     = -2,
        ZERO_LENGTH_OUTPUT_PATH = -3,
        MISSING_POS_ARG         = -4,
        TOO_MANY_POS_ARGS       = -5,
        
        FORCE_BREAK = -100,  // For internal use when slurping only
        CONTINUE    = -101,  // For internal use when slurping only
        GARBAGE_SUCCESS_WITH_ERR_FLAG   = -1000,
        GARBAGE_ERR_WITHOUT_ERR_FLAG    = -1001,
};

int slurpIndex = 1;

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

enum SlurpErr getPosArgs(struct Slurped *out, int argc, char **argv, int i) 
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

        // We have exactly as mant positional arguments as we expect.
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
                out->flags = (out->flags | VERSION_FLAG);
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

enum SlurpErr checkOpt(struct Slurped *out, int argc, char **argv) 
{
        const int i = slurpIndex;
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

        if ((streq(arg, "--quiet", 8) || streq(arg, "--error", 8))) {
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
        
        if ((streq(arg, "-p", 3) || streq(arg, "--prompt", 9))) {
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

        if ((streq(arg, "-o", 3) || streq(arg, "--output", 9))) {
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

        return checkHelpOrVers(out, arg);
}

enum SlurpErr handleArg(struct Slurped *out, int argc, char **argv) 
{
        const int i = slurpIndex;
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

enum SlurpErr slurpArgs(struct Slurped *out, int argc, char **argv) 
{
        out->flags = 0;
        out->outputLen = 0;
        out->posArg1Len = 0;
        out->posArg2Len = 0;

        if (argc == 1 || argc >= LOOP_MAX) {
                out->flags = (out->flags | HELP_FLAG);
                return SLURP_SUCCESS;
        }

        int forceBreak = 0;
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
        if ((out->flags & HELP_FLAG) || (out->flags & VERSION_FLAG)) {
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
enum SlurpErr displayErr(uint32_t flags, char **argv, enum SlurpErr err, int i) 
{
        const int hasFlag = (flags & ERROR_FLAG);

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
                normal("   \\___ e.g. \"--option path/file.ex\" rather than just \"path/file.ex\"\n");
                normal(" \\___ Verify that all options have '-' or '--' before them\n");
                normal("   \\__ e.g. \"--prompt\" or \"-p\", rather than just \"prompt\"\n");
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

int putFile(char *fileName)
{
        FILE *file = fopen(fileName, "r");

        if (file == NULL) {
                error("Error while displaying: Could not load the file (null at %s)", 
                        fileName);
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
                error("Error while displaying: Standard error code <%d>", 
                        ferror(file));
                fclose(file);
                return EXIT_FAILURE;
        }

        fclose(file);
        return EXIT_SUCCESS;
}

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
                const char helpFile[] = "\\delta-help-long.txt";
                char *path = malloc(sizeof(helpFile) + sizeof(char) * homeLen);
                strncpy(path, home, sizeof(char) * homeLen);
                strncat(path, helpFile, sizeof(helpFile));
                const int ret = putFile(path);
                free(path);
                return ret;
        } else {
                const char helpFile[] = "\\delta-help-short.txt";
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

// Attempts to open a read-only file, displaying a message if the file cannot 
// be opened. If the file cannot be opened, this returns NULL. The file must be 
// closed with `fclose()` after usage
FILE *attemptRFileOpen(char *filename, int maxCount) 
{
        // Getting the file name 
        char *subName = malloc(sizeof(char) * (maxCount + 1));
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

FILE *createWFile(char *filename) {
        FILE *file = fopen(filename, "wb");
        if (file == NULL) {
                error("Error while create file: Could not create file \"s\" for writing\n",
                        filename);
                return NULL;
        }
        verbose("Created file \"%s\"\n", filename);
        return file;
}

// Attempts to open or create a file for writing, displaying a message if the 
// file cannot be opened. The flags argument specifies whether to override
// an existing file, to prompt beforehand, or to only ever create new ones.
FILE *attemptWFileOpen(char *filename, int maxCount, uint32_t flags) 
{
        // Getting the file name 
        char *subName = malloc(sizeof(char) * (maxCount + 1));
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
        const int closeRet = fclose(file);
        if (closeRet != 0) {
                error("Error while creating file: Could not close \"%s\"\n", 
                        subName);
                free(subName);
                return NULL;
        }

        const int force = flags & DESTINATION_DELETE_FLAG;
        const int prompt = (flags & PROMPT_FLAG) && !(flags & SILENT_FLAG);
        
        char userOpinion = 'n';
        if (prompt) {
                // Get the user's opinion from stdin
                // This always occurs with the prompt flag, even if force is set
                loud("Would you like to override all content in \"%s\"? \n",
                        subName);
                loud("y/n (default is 'n') > ");
                int resp = getchar();
                debug("Response was 0x%08x \n", resp);
                if (resp == EOF || ferror(stdin)) {
                        loud("Warning: user input had an error (defaulting to 'n')\n");
                }

                if (resp == 'y' || resp == 'Y') {
                        userOpinion = 'y';
                }
        }

        if (prompt && userOpinion != 'y') {
                // User says to NOT delete the file
                // Respect their opinion (and don't inform them that they can 
                // override this behavior)
                file = NULL;
                normal(" \\___ Cancelled.\n");
        }

        if (!prompt && !force) {
                // No flags were set, so take the safest option.
                // Unlike if the user opinionates 'n', we inform them
                // that this behavior can be overridden with flags
                // The user's opinion is to not delete or there were no flags,
                // so we exit the program.
                file = NULL;
                error("Error while creating file: File \"%s\" already exists\n",
                        subName);

                if (!prompt) {
                        normal(" \\___ To ask to override the file, use the -p or --prompt command flag\n");
                }
        }

        if ((!prompt && force) || (prompt && userOpinion == 'y')) {
                // Only told to force delete, so do it
                file = createWFile(subName);
                normal(" \\___ Previous contents of the file were overridden\n");
        }

        free(subName);
        return file;
}

struct FileBin {
        uint64_t size;
        uint8_t *buf;
};

// Puts the entire contents of a read-only file into a byte buffer.
// This buffer MUST be freed after its used. This returns NULL if any error 
// occurs
struct FileBin *readBin(char *filename, int filenameLen)
{
        // NOTE: This uses the Win _fileno() function to create a buffer
        // Opening the src file and reading from
        FILE *src = attemptRFileOpen(filename, filenameLen);
        if (src == NULL) {
                return NULL;
        }

        const long srcSize = _filelength(_fileno(src));
        if (srcSize <= 0) {
                error("Error while reading src: Source file length was 0 or an error.\n");
                // TODO: Err code  should be set somewhere
                return NULL;
        }
        uint8_t *srcBuf = malloc(srcSize);
        if (srcBuf == NULL) {
                error("Error while reading src: Failed to allocate %d bytes\n", 
                        srcSize);
                free(srcBuf);
                // TODO: Failure should be set somewhere
                return NULL;
        }

        normal("Reading from \"%s\"... (this may take awhile)\n", filename);
        const int srcRead = fread((void *) srcBuf, 1, srcSize, src);
        if (srcRead != srcSize) {
                error("Error while reading src: Expected to read %d bytes; read %d\n", 
                        srcSize, srcRead);
                normal(" \\___ Reason: %s\n", strerror(errno));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        if (ferror(src)) {
                error("Error while reading src: %s\n", strerror(ferror(src)));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        fclose(src);

        if (ferror(src)) {
                error("Error while reading src: %s\n", strerror(ferror(src)));
                free(srcBuf);
                // TODO: Err code should be set somewhere
                return NULL;
        }

        struct FileBin *result = malloc(sizeof(struct FileBin));
        result->size = srcSize;
        result->buf = srcBuf;
        return result;
}

struct LinkedCommand {
        struct Command *elem;
        struct LinkedCommand *next;
};

int freeLinked(struct LinkedCommand *head) {
        struct LinkedCommand *cur = head;
        int i = 0;
        while (cur != NULL) {
                struct LinkedCommand *prev = cur;
                cur = cur->next;
                free(prev->elem);
                free(prev);
                i++;
        }
        return i;
}

int computeDelta(struct Slurped *args) 
{
        // Getting our output file
        // This is done first to prevent postponing any errors or prompting
        // for a time when the user has already waited minutes (or hours)
        // for delta computation to have finished
        FILE *outFile = attemptWFileOpen(args->outputFileName, args->outputLen, 
                args->flags);
        if (outFile == NULL) {
                verbose("Cancelled the delta computation\n");
                return EXIT_FAILURE;
        }

        // Reading the contents of our src files into buffers
        struct FileBin *s = readBin(args->posArg1, args->posArg1Len);
        if (s == NULL) {
                verbose("Cancelled the delta computation.\n");
                fclose(outFile); // Doesn't really matter if this fails
                return EXIT_FAILURE; // TODO: error code.
        }
        
        struct FileBin *t = readBin(args->posArg2, args->posArg2Len);
        if (t == NULL) {
                verbose("Cancelled the delta computation.\n");
                fclose(outFile); // Doesn't really matter if this fails
                free(s->buf);
                free(s);
                return EXIT_FAILURE; // TODO: error code.
        }

        // Computing the commands
        // We store the 
        normal("Computing ... (takes a lot of time)\n");
        struct LinkedCommand head = { .elem = NULL, .next = NULL };
        struct LinkedCommand *last = &head;
        uint64_t q = 0;
        uint64_t outSize = 0;

        while (q < t->size) {
                normal(" \\___ %d / %d (%.2f%%)\n", q, t->size, 
                        100.0f * (float) q / (float) t->size);

                // TODO: Start from the last p.
                struct Command *command = nextLargestMove(s->buf, 0, s->size, 
                        &(t->buf[q]), t->size - q);

                if (command->type == ADD_COMMAND) {
                        // NOTE: curIndex must be set by the consumer
                        command->cmd.add.curIndex.longVal = q;
                        if (logFlags & VERBOSE_FLAG) {
                                const char c = (char) (command->cmd.add.symbol);
                                const uint8_t qSet = command->cmd.add.curIndex.longVal;
                                verbose("   \\___ Command: ADD '%c' at %d \n", c, qSet);
                        }
                        
                } else {
                        // NOTE: curIndex must be set by the consumer
                        command->cmd.move.curIndex.longVal = q;
                        if (logFlags & VERBOSE_FLAG) {
                                const uint64_t pSet = command->cmd.move.prevIndex.longVal;
                                const uint64_t qSet = command->cmd.move.curIndex.longVal;
                                const uint64_t l    = command->cmd.move.len.longVal;
                                verbose("   \\___ Command: MOVE %d -> %d (length %d) \n", pSet, qSet, l);
                        }
                }

                struct LinkedCommand *append = malloc(sizeof(struct LinkedCommand));
                append->elem = command;
                append->next = NULL;
                last->next = append; // Appending our node onto the last node
                last = append; // Making our node the last node.
                q += patchSizeOf(command);
                outSize += serialSizeOf(command);
        }

        debug("Ended command parsing\n");

        //#region DEV START: Printing the linked list chain
        if (DEBUG) {
                struct LinkedCommand *cur = &head;
                int i = 0;
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
        //#endregion DEV END
        
        debug("Freeing the s buffer\n");
        free(s->buf);
        debug("Freeing the s FileBin struct\n");
        free(s);
        debug("Freeing the t buffer\n");
        free(t->buf);
        debug("Freeing the t FileBin struct\n");
        free(t);

        // Serializing the commands
        normal("Serializing the commands... (this may take a while)\n");
        verbose("Attempting to allocate %d bytes...\n", outSize);
        uint8_t *outBuf = (uint8_t *) malloc(outSize);
        uint64_t outIndex = 0;
        if (outBuf == NULL) {
                error("Error while serializing: Could not allocate output buffer (size = %d bytes)\n",
                        outSize);
                fclose(outFile); // Doesn't really matter if this fails
                freeLinked(head.next);
                return EXIT_FAILURE;
        }
        verbose("Finished allocation\n");
        struct LinkedCommand *cur = head.next;
        debug("Starting linked list traversal\n");
        while (cur != NULL) {
                const struct Command *command = cur->elem;
                debug("Trying to serialize the specific command\n");
                if (command != NULL) {
                        debug("Non-null (type '%c')\n", command->type);
                        const uint32_t expectedSize = serialSizeOf(command);
                        verbose(" \\___ Serializing command with type '%c' and serial size %d\n",
                                command->type, expectedSize);                        
                        const uint32_t wrote = serializeCommand(outBuf, outSize, 
                                outIndex, command);
                        if (wrote < expectedSize) {
                                error("Error while serializing: Expected to write %d bytes for command type '%c'; wrote %d\n",
                                        expectedSize, command->type, wrote);
                                free(outBuf);
                                freeLinked(cur);
                                return EXIT_FAILURE;
                        }
                        outIndex += expectedSize;
                } else {
                        debug("Null\n");
                        verbose(" \\___ Command member was NULL\n");
                }

                struct LinkedCommand *prev = cur;
                cur = cur->next;
                debug("Moved to the next linked node\n");
                free(prev->elem);
                free(prev);
        }
        debug("Starting\n");

        // Output buffer
        // TODO: Serialize header
        normal("Writing the serialized commands buffer to a file...\n");
        const int written = fwrite(outBuf, 1, outSize, outFile);
        if (written != outSize) {
                error("Error while writing delta: Expected to write %d bytes; wrote %d\n", 
                        outSize, written);
                normal(" \\___ Reason: %s\n", strerror(errno));
                free(outBuf);
                fclose(outFile); // Doesn't really matter if this fails
                return EXIT_FAILURE; // TODO: Error code
        }

        if (ferror(outFile)) {
                error("Error while reading src: %s\n", strerror(ferror(outFile)));
                free(outBuf);
                fclose(outFile); // Doesn't really matter if this fails
                return EXIT_FAILURE; // TODO: Error code
        }

        // Cleaning up
        normal("Finished outputing the delta\n");
        free(outBuf);
        fclose(outFile); // Doesn't really matter if this fails
        return EXIT_SUCCESS;
}

int reconstructTarget(struct Slurped *args) 
{      
        // TODO: Reconstruct
        return EXIT_SUCCESS;
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
        debug("Output Path: %s (length %d)\n", slurpedPtr->outputFileName, slurpedPtr->outputLen);
        debug("Pos arg 1: %s (length %d)\n", slurpedPtr->posArg1, slurpedPtr->posArg1Len);
        debug("Pos arg 2: %s (length %d)\n", slurpedPtr->posArg2, slurpedPtr->posArg2Len);

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
        logFlags = flags & (VERBOSE_FLAG | QUIET_FLAG | SILENT_FLAG);
        const int argErr = displayErr(flags, argv, slurpErr, slurpIndex);
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