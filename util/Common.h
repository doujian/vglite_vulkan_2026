#ifndef __SFT_COMMON_H__
#define __SFT_COMMON_H__

#include "vg_lite.h"

#define IS_ERROR(status)         (status > 0)
#define CHECK_ERROR(Function) \
    error = Function; \
    if (IS_ERROR(error)) \
    { \
        printf("[%s: %d] error type is %s\n", __func__, __LINE__, error_type[error]);\
        goto ErrorHandler; \
    }

extern char *error_type[];

#endif
