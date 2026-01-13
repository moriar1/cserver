#ifndef CUSTOM_H
#define CUSTOM_H

#ifndef NDEBUG // Debug build

#define DEBUG_PRINTF(fmt, ...)                                                 \
  do {                                                                         \
    fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __func__, __LINE__,            \
            __VA_ARGS__);                                                      \
  } while (0)
#define DEBUG_PUTS(msg)                                                        \
  do {                                                                         \
    fprintf(stderr, "[DEBUG] %s:%d: " msg "\n", __func__, __LINE__);           \
  } while (0)

#else // Release build

#define DEBUG_PRINTF(fmt, ...) ((void)0)
#define DEBUG_PUTS(msg) ((void)0)

#endif // NDEBUG
#endif // CUSTOM_H
