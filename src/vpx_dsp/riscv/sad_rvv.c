/* libvpx RVV：SAD 内核（vpx_dsp/riscv/sad_rvv.c）
 *
 * 语义与 vpx_dsp/sad.c 里的 static sad() 完全一致：逐像素绝对差之和，返回 unsigned。
 * abs(a-b) 在 u8 上不可直接做（会回绕），所以用 max/min 相减：
 *   hi = vmaxu(a,b); lo = vminu(a,b); d = hi - lo;  d 必然 [0,255]
 * 再把 u8 宽度归约成 u16 累加，避免标量逐像素循环。
 */
#include <riscv_vector.h>
#include <stdint.h>

#include "./sad_rvv.h"

static unsigned sad_rvv(const uint8_t *src, int src_stride,
                        const uint8_t *ref, int ref_stride,
                        int width, int height) {
  unsigned total = 0;
  int y;
  for (y = 0; y < height; y++) {
    int x = 0;
    while (x < width) {
      size_t vl = __riscv_vsetvl_e8m1((size_t)(width - x));
      vuint8m1_t a = __riscv_vle8_v_u8m1(src + x, vl);
      vuint8m1_t b = __riscv_vle8_v_u8m1(ref + x, vl);
      vuint8m1_t hi = __riscv_vmaxu_vv_u8m1(a, b, vl);
      vuint8m1_t lo = __riscv_vminu_vv_u8m1(a, b, vl);
      vuint8m1_t d = __riscv_vsub_vv_u8m1(hi, lo, vl);
      /* 单行最多 width*255；64x64 一行 16320 仍 < 65535，u16 归约不会溢出 */
      /* vwredsumu 在 GCC 的 1.0 intrinsic 里返回 u16m1 向量（结果在元素 0），
         所以还要 vmv.x.s 取标量，否则类型对不上。 */
      total += (unsigned)__riscv_vmv_x_s_u16m1_u16(__riscv_vwredsumu_vs_u8m1_u16m1(
          d, __riscv_vmv_v_x_u16m1((uint16_t)0, vl), vl));
      x += (int)vl;
    }
    src += src_stride;
    ref += ref_stride;
  }
  return total;
}

unsigned vpx_sad8x8_rvv(const uint8_t *src, int src_stride,
                        const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 8, 8);
}
unsigned vpx_sad16x16_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 16, 16);
}
unsigned vpx_sad32x32_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 32, 32);
}
unsigned vpx_sad64x64_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 64, 64);
}
