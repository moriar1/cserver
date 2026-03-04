#ifndef CUSTOM_LOG_H
#define CUSTOM_LOG_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h> // abort()
#include <string.h>
#include <time.h>

// Internal
#define LOG_IMPL(level, fmt, ...)                                              \
  do {                                                                         \
    struct timespec ts;                                                        \
    clock_gettime(CLOCK_REALTIME, &ts);                                        \
    struct tm tm;                                                              \
    localtime_r(&ts.tv_sec, &tm);                                              \
    fprintf(stderr, "[%02d:%02d:%02d.%03ld] [%s] %s:%d: " fmt "\n",            \
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, level,     \
            __func__, __LINE__, ##__VA_ARGS__);                                \
  } while (0)
#define LOG_ERRNO_IMPL(level, fmt, ...)                                        \
  do {                                                                         \
    struct timespec ts;                                                        \
    clock_gettime(CLOCK_REALTIME, &ts);                                        \
    struct tm tm;                                                              \
    localtime_r(&ts.tv_sec, &tm);                                              \
    const int saved_errno__ = errno;                                           \
    char my_buf__[128];                                                        \
    strerror_r(saved_errno__, my_buf__, sizeof my_buf__);                      \
    fprintf(stderr, "[%02d:%02d:%02d.%03ld] [%s] %s:%d: " fmt ": %s\n",        \
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, level,     \
            __func__, __LINE__, ##__VA_ARGS__, my_buf__);                      \
  } while (0)

// Interface
#define LOG_INFO(fmt, ...) LOG_IMPL("INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_IMPL("ERROR", fmt, ##__VA_ARGS__)
#define LOG_ERRNO(fmt, ...) LOG_ERRNO_IMPL("ERROR", fmt, ##__VA_ARGS__)

// NOTE: abort() in FATAL
#define LOG_FATAL(fmt, ...)                                                    \
  do {                                                                         \
    LOG_IMPL("FATAL", fmt, ##__VA_ARGS__);                                     \
    abort();                                                                   \
  } while (0)
#define LOG_FATAL_ERRNO(fmt, ...)                                              \
  do {                                                                         \
    LOG_ERRNO_IMPL("FATAL", fmt, ##__VA_ARGS__);                               \
    abort();                                                                   \
  } while (0)

// logging for `Debug build` and do nothing for `Release build`
#ifndef NDEBUG // Debug build
#define LOG_DEBUG(fmt, ...) LOG_IMPL("DEBUG", fmt, ##__VA_ARGS__)
#define LOG_DEBUG_ERRNO(fmt, ...) LOG_ERRNO_IMPL("DEBUG", fmt, ##__VA_ARGS__)
#else // Release build
#define LOG_DEBUG(fmt, ...) ((void)(0 && (fmt, ##__VA_ARGS__)))
#define LOG_DEBUG_ERRNO(fmt, ...) ((void)(0 && (fmt, ##__VA_ARGS__)))

#endif
#endif // CUSTOM_LOG_H
