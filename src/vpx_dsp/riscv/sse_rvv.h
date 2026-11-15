/*
 * RVV SSE (sum of squared error) 内核 —— G3。
 * 与 C 版逐位一致：整数运算，没有浮点舍入问题。
 */
#ifndef VPX_DSP_RISCV_SSE_RVV_H_
#define VPX_DSP_RISCV_SSE_RVV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t vpx_sse_rvv(const uint8_t *a, int a_stride, const uint8_t *b,
                    int b_stride, int width, int height);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_DSP_RISCV_SSE_RVV_H_
