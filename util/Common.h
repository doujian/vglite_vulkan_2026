#ifndef __SFT_COMMON_H__
#define __SFT_COMMON_H__

#include "vg_lite.h"

#define IS_ERROR(status)         ((status) > 0)
#define CHECK_ERROR(Function) \
    do { \
        error = (Function); \
        if (IS_ERROR(error)) { \
            printf("[%s:%d] error=%d\n", __func__, __LINE__, error); \
            goto ErrorHandler; \
        } \
    } while (0)

#endif
