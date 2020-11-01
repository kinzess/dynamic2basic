#include <stdio.h>

#ifdef DEBUG
#define D(...)                                                                 \
    do {                                                                       \
        fprintf(stdout, "[DEBUG] %s %s(Line %d): \n", __FILE__, __FUNCTION__,  \
                __LINE__);                                                     \
        fprintf(stdout, __VA_ARGS__);                                          \
    } while (0)
#else
#define D(...)
#endif