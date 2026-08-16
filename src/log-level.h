#ifndef LOG_LEVEL_H
#define LOG_LEVEL_H

// Whether to show debug(...) printf messages. Does not affect LogLevels 
// provided by flag
#define DEBUG 1

#include <stdint.h>

// Describes what messages are displayed, and what are hidden. All constants
// can fit within 3 bits.
//
// Higher values mean more messages are displayed. The inequality is 
// `SILENT < ERRORS < WARNS < REDUCED < INFO < DETAIL < VERBOSE`. 
// 
// Only the properties described (bit width, the inequality) are guaranteed.
enum LogLevel {
    SILENT_LVL = 0, // Literally NOTHING is shown
    ERRORS_LVL,     // Only errors (and warnings in --strict) are shown.
    WARNINGS_LVL,   // Only warnings and errors are shown
    REDUCED_LVL,    // Exclude details, verbosity, and most info.
    INFO_LVL,       // Exclude verbosity and details. This is the default
    DETAIL_LVL,     // Exclude only verbosity.
    VERBOSE_LVL     // Show EVERYTHING (well, except debug messages)
};

enum LogLevel getLogLevel(void);

void setLogLevel(enum LogLevel newFlags);

// Prints the format if and only if the DEBUG macro is 1.
void debug(const char *format, ...);

// Prints the format to stdout iff log level is verbose
int32_t verbose(const char *format, ...);

// Prints the format to stdout iff the log level is detail or verbose
int32_t detail(const char *format, ...);

// Prints the format to stdout iff the log level is info or higher 
int32_t info(const char *format, ...);

// Prints the format to stdout iff the log level is reduced or higher
int32_t loud(const char *format, ...);

// Prints the format to stderr iff the log level is neither silent nor error 
int32_t warn(const char *format, ...);

// Prints the format to stdout iff the log level is not silent
int32_t prompt(const char *format, ...);

// Prints the format to stderr iff the log level is not silent 
int32_t error(const char *format, ...);

#endif