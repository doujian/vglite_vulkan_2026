#ifndef VG_LITE_MATH_H
#define VG_LITE_MATH_H

#include <math.h>

static inline void mat3_multiply(const float a[3][3], const float b[3][3], float out[3][3])
{
    float tmp[3][3] = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                tmp[i][j] += a[i][k] * b[k][j];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            out[i][j] = tmp[i][j];
}

static inline int mat3_inverse(const float m[3][3], float inv[3][3])
{
    float det = m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])
              - m[0][1]*(m[1][0]*m[2][2] - m[1][2]*m[2][0])
              + m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    if (fabsf(det) < 1e-7f) return 0;

    float idet = 1.0f / det;
    inv[0][0] = (m[1][1]*m[2][2] - m[1][2]*m[2][1]) * idet;
    inv[0][1] = (m[0][2]*m[2][1] - m[0][1]*m[2][2]) * idet;
    inv[0][2] = (m[0][1]*m[1][2] - m[0][2]*m[1][1]) * idet;
    inv[1][0] = (m[1][2]*m[2][0] - m[1][0]*m[2][2]) * idet;
    inv[1][1] = (m[0][0]*m[2][2] - m[0][2]*m[2][0]) * idet;
    inv[1][2] = (m[0][2]*m[1][0] - m[0][0]*m[1][2]) * idet;
    inv[2][0] = (m[1][0]*m[2][1] - m[1][1]*m[2][0]) * idet;
    inv[2][1] = (m[0][1]*m[2][0] - m[0][0]*m[2][1]) * idet;
    inv[2][2] = (m[0][0]*m[1][1] - m[0][1]*m[1][0]) * idet;
    return 1;
}

#endif
