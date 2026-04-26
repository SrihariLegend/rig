#ifndef PI_LOG_H
#define PI_LOG_H

#include <stdio.h>
#include <time.h>
#include <string.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

extern LogLevel pi_log_level;
extern FILE *pi_log_file;

static inline void pi_log_set_level(LogLevel level) { pi_log_level = level; }

static inline void pi_log_open(const char *path) {
    if (pi_log_file && pi_log_file != stderr) fclose(pi_log_file);
    pi_log_file = fopen(path, "a");
}

static inline void pi_log_close(void) {
    if (pi_log_file && pi_log_file != stderr) { fclose(pi_log_file); pi_log_file = NULL; }
}

#define PI_LOG(level, tag, ...) do { \
    if (pi_log_level <= (level)) { \
        FILE *_f = pi_log_file ? pi_log_file : stderr; \
        time_t _t = time(NULL); \
        struct tm *_tm = localtime(&_t); \
        char _ts[20]; \
        strftime(_ts, sizeof(_ts), "%H:%M:%S", _tm); \
        fprintf(_f, "%s [%s] ", _ts, tag); \
        fprintf(_f, __VA_ARGS__); \
        fprintf(_f, "\n"); \
        fflush(_f); \
    } \
} while(0)

#define LOG_DEBUG(...) PI_LOG(LOG_DEBUG, "DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  PI_LOG(LOG_INFO,  "INFO ", __VA_ARGS__)
#define LOG_WARN(...)  PI_LOG(LOG_WARN,  "WARN ", __VA_ARGS__)
#define LOG_ERROR(...) PI_LOG(LOG_ERROR, "ERROR", __VA_ARGS__)

#endif
