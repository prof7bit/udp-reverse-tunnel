#ifndef MISC_H
#define MISC_H

#include <stdint.h>

typedef enum {
    LOG_ERROR = 3,
    LOG_INFO = 6,
    LOG_DEBUG = 7
} log_level_t;

uint64_t millisec();
void print(log_level_t level, char* fmt, ...);

#endif
