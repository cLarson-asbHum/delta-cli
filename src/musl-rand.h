#ifndef MUSL_RAND_h
#define MUSL_RAND_H 

#include <stdint.h>

void musl_srand(uint32_t seed);

uint32_t musl_rand(void);

#endif