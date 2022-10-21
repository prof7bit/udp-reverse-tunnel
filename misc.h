#ifndef MISC_H
#define MISC_H

#include <stdint.h>

typedef enum {
    LOG_EMERGENCY = 0,
    LOG_ALERT = 1,
    LOG_CRITICAL = 2,
    LOG_ERROR = 3,
    LOG_WARN = 4,
    LOG_NOTICE = 5,
    LOG_INFO = 6,
    LOG_DEBUG = 7
} log_level_t;

uint64_t millisec();
void print(log_level_t level, char* fmt, ...);
void print_e(log_level_t level, char* fmt, ...);

#endif
