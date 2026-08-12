#ifndef DELTA_COMMAND_H
#define DELTA_COMMAND_H

#include "slurp-arg.h"
#include "delta.h"

void verboseCmdLog(const struct Command *command);

int32_t computeDelta(const struct Slurped *args);

#endif