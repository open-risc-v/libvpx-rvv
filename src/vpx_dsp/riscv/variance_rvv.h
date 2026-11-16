#ifndef VPX_VPX_DSP_RISCV_VARIANCE_RVV_H_
#define VPX_VPX_DSP_RISCV_VARIANCE_RVV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 1 表示这个 (w,h) 走 RVV，否则由调用方回落 C。 */
int vpx_variance_rvv_applicable(int w, int h);

/* 与 vpx_dsp/variance.c 里的 static variance() 逐位等价：
   同时输出 sum(diff) 与 sum(diff^2)，纯整数、无舍入，所以上层
   `sse - sum*sum/(w*h)` 的结果天然与 C 完全相同。 */
void vpx_variance_rvv(const uint8_t *src, int src_stride, const uint8_t *ref,
                      int ref_stride, int w, int h, uint32_t *sse, int *sum);

#ifdef __cplusplus
}
#endif

#endif // VPX_VPX_DSP_RISCV_VARIANCE_RVV_H_
