#include <stdio.h>
#include <stdarg.h>
#include "log-level.h"
#include "slurp-arg.h"

enum LogLevel logLvl = 0;

enum LogLevel getLogLevel(void) 
{
        return logLvl;
}

void setLogLevel(enum LogLevel lvl) 
{
        logLvl = (lvl & 7);
}

void debug(const char *format, ...) 
{
                if (DEBUG) {
                va_list args;
                va_start(args, format);
                const int32_t ret = vfprintf(stdout, format, args);
                va_end(args);
        }
}

int32_t verbose(const char *format, ...) 
{
        if (logLvl < VERBOSE_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

int32_t detail(const char *format, ...) 
{
        if (logLvl < DETAIL_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

int32_t info(const char *format, ...) 
{
        if (logLvl < INFO_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

int32_t loud(const char *format, ...) 
{
        if (logLvl < REDUCED_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

int32_t warn(const char *format, ...) 
{
        if (logLvl < WARNINGS_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stderr, format, args);
        va_end(args);
        return ret;
}


int32_t prompt(const char *format, ...) 
{
        if (logLvl < ERRORS_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stdout, format, args);
        va_end(args);
        return ret;
}

int32_t error(const char *format, ...) 
{
        if (logLvl < ERRORS_LVL) {
                return 0;
        }

        va_list args;
        va_start(args, format);
        const int32_t ret = vfprintf(stderr, format, args);
        va_end(args);
        return ret;
}
