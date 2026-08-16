#ifndef SLURP_ARG_H
#define SLURP_ARG_H

#include <stdint.h>
#include "log-level.h"

#define MAX_FILE_PATH_LEN 512

#define HELP_FLAG               (1u << (0))
#define SHOW_VERSION_FLAG       (1u << (1))
#define DELTA_FLAG              (1u << (2))
#define RECONSTRUCT_FLAG        (1u << (3))
#define LOG_MODE_BIT_0          (1u << (4))
#define LOG_MODE_BIT_1          (1u << (5))
#define LOG_MODE_BIT_2          (1u << (6))
#define OUTPUT_FLAG             (1u << (7))
#define DESTINATION_DELETE_FLAG (1u << (8))
#define IGNORE_HASH_FLAG        (1u << (9))
#define GARBAGE_EASTER_EGG_FLAG (1u << (10))
#define PROMPT_FLAG             (1u << (11))
#define PRESERVE_FLAG           (1u << (12))
#define WARNINGS_AS_ERRORS_FLAG (1u << (13))
#define FILE_VERSION_FLAG       (1u << (14))
#define ERROR_FLAG              (1u << (30))

struct Slurped {
        uint32_t flags;
        uint16_t outputLen;
        char *outputFileName;
        int16_t version;
        uint16_t posArg1Len;
        char *posArg1;
        uint16_t posArg2Len;
        char *posArg2;
};

enum SlurpErr {
        SLURP_SUCCESS = 0,
        
        UNKNOWN_COMMAND         = -1,
        UNKNOWN_FLAG_OR_ARG     = -2,
        ZERO_LENGTH_OUTPUT_PATH = -3,
        MISSING_POS_ARG         = -4,
        TOO_MANY_POS_ARGS       = -5,
        MALFORMED_FILE_VERSION  = -6,
        UNKNOWN_FILE_VERSION    = -7,
        
        FORCE_BREAK = -100,  // For internal use when slurping only
        CONTINUE    = -101,  // For internal use when slurping only
        GARBAGE_SUCCESS_WITH_ERR_FLAG   = -1000,
        GARBAGE_ERR_WITHOUT_ERR_FLAG    = -1001,
};

uint32_t getSlurpIndex(void);

uint8_t minPosArgs(uint32_t flags);

uint8_t maxPosArgs(uint32_t flags);

uint32_t logLevelToFlags(enum LogLevel lvl);

enum LogLevel flagsToLogLevel(uint32_t flags);

enum SlurpErr slurpArgs(struct Slurped *out, int32_t argc, char **argv);

// Displays a message for the slurpErr variable. Returns SLURP_SUCCESS if there 
// was no error to begin with
enum SlurpErr displayErr(uint32_t flags, char **argv, enum SlurpErr err, 
        uint32_t i);

// Returns 0 if the --strict flag (or equivalent) is not set; non-zero otherwise
uint32_t warnIsErr(uint32_t flags);

#endif