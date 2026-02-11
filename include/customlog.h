#ifndef CUSTOM_LOG_H
#define CUSTOM_LOG_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h> // abort()
#include <string.h>

// Internal
#define LOG_IMPL(level, fmt, ...)                                              \
  do {                                                                         \
    fprintf(stderr, "[%s] %s:%d: " fmt "\n", level, __func__, __LINE__,        \
            ##__VA_ARGS__);                                                    \
  } while (0)
#define LOG_ERRNO_IMPL(level, fmt, ...)                                        \
  do {                                                                         \
    const int saved_errno = errno;                                             \
    fprintf(stderr, "[%s] %s:%d: " fmt ": %s\n", level, __func__, __LINE__,    \
            ##__VA_ARGS__, strerror(saved_errno));                             \
  } while (0)

// Interface
#define LOG_INFO(fmt, ...) LOG_IMPL("INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_IMPL("ERROR", fmt, ##__VA_ARGS__)
#define LOG_ERRNO(fmt, ...) LOG_ERRNO_IMPL("ERROR", fmt, ##__VA_ARGS__)

// NOTE: abort() in FATAL
#define LOG_FATAL(fmt, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, "[FATAL] %s:%d: " fmt "\n", __func__, __LINE__,            \
            ##__VA_ARGS__);                                                    \
    abort();                                                                   \
  } while (0)
#define LOG_FATAL_ERRNO(fmt, ...)                                              \
  do {                                                                         \
    const int saved_errno = errno;                                             \
    fprintf(stderr, "[FATAL] %s:%d: " fmt ": %s\n", __func__, __LINE__,        \
            ##__VA_ARGS__, strerror(saved_errno));                             \
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
