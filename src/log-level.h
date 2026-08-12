#ifndef LOG_LEVEL_H
#define LOG_LEVEL_H

#define DEBUG 0

#include <stdint.h>

uint32_t getLogFlags(void);

void setLogFlags(uint32_t newFlags);

// Prints the format if and only if the DEBUG macro is 1.
void debug(const char *format, ...);

// Prints the format to stdout if and only if verbose mode is enabled
int32_t verbose(const char *format, ...);

// Prints the format to stdout if neither quiet nor silent mode are enabled
int32_t normal(const char *format, ...);

// Prints the format to stdout if and only if silent is not enabled. 
int32_t loud(const char *format, ...);

// Prints the format to stderr if and only if silent is not enabled. 
int32_t error(const char *format, ...);

#endif