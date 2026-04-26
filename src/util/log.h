#ifndef PI_LOG_H
#define PI_LOG_H

#include <stdio.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

static LogLevel pi_log_level = LOG_INFO;

static inline void pi_log_set_level(LogLevel level) { pi_log_level = level; }

#define LOG_DEBUG(...) do { if (pi_log_level <= LOG_DEBUG) { fprintf(stderr, "[DEBUG] " __VA_ARGS__); fprintf(stderr, "\n"); } } while(0)
#define LOG_INFO(...)  do { if (pi_log_level <= LOG_INFO)  { fprintf(stderr, "[INFO]  " __VA_ARGS__); fprintf(stderr, "\n"); } } while(0)
#define LOG_WARN(...)  do { if (pi_log_level <= LOG_WARN)  { fprintf(stderr, "[WARN]  " __VA_ARGS__); fprintf(stderr, "\n"); } } while(0)
#define LOG_ERROR(...) do { if (pi_log_level <= LOG_ERROR) { fprintf(stderr, "[ERROR] " __VA_ARGS__); fprintf(stderr, "\n"); } } while(0)

#endif
