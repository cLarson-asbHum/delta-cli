#include <stdio.h>
#include <stdarg.h>
#include "log-level.h"
#include "slurp-arg.h"

uint32_t logFlags = 0;

uint32_t getLogFlags(void) {
        return logFlags;
}

void setLogFlags(uint32_t newFlags) {
        logFlags = newFlags;
}

// Prints the format if and only if the DEBUG macro is 1.
void debug(const char *format, ...) {
        // Hopefully the compiler can optimize enough to remove this.
        if (DEBUG) {
                va_list args;
                va_start(args, format);
                const int32_t ret = vfprintf(stdout, format, args);
                va_end(args);
        }
}

// Prints the format to stdout if and only if verbose mode is enabled
int32_t verbose(const char *format, ...) {
        if ((logFlags & VERBOSE_FLAG)) {
                va_list args;
                va_start(args, format);
                const int32_t ret = vfprintf(stdout, format, args);
                va_end(args);
                return ret;
        }

        return 0;
}

// Prints the format to stdout if neither quiet nor silent mode are enabled
int32_t normal(const char *format, ...) {
        if ((logFlags & QUIET_FLAG) || (logFlags & SILENT_FLAG)) {
               return 0; 
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

// Prints the format to stdout if and only if silent is not enabled. 
int32_t loud(const char *format, ...) {
        // TODO: Reduced logging mode (between normal and error)
        if (logFlags & SILENT_FLAG) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

// Prints the format to stderr if and only if silent is not enabled. 
int32_t error(const char *format, ...) {
        if (logFlags & SILENT_FLAG) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stderr, format, args);
        va_end(args);
        return ret;
}