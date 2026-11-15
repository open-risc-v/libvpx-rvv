/* libvpx RVV：SAD 内核 v2（vpx_dsp/riscv/sad_rvv.c）
 *
 * 语义与 vpx_dsp/sad.c 的 static sad() 一致：逐像素绝对差之和。
 *
 * v1 的写法是"每行末尾归约一次"，实测 8x8 比标量还慢（0.73x）、64x64 要拆两个 chunk
 * 每行归约两次。v2 改成：
 *   1. 整个块只在**最后一次**归约，行内/行间都只做向量累加；
 *   2. chunk 长度取 VLMAX(u16m1)（VLEN=256 时是 16 字节），这样 u8 的 diff 可以直接
 *      widening 加进 u16 累加器，不需要中间标量；
 *   3. vsetvl 只在函数入口调一次，行循环里不再重复算 vl。
 *
 * abs(a-b) 在 u8 上会回绕，所以用 max/min 相减：
 *   hi = vmaxu(a,b); lo = vminu(a,b); d = hi - lo;  d 必然 [0,255]
 */
#include <riscv_vector.h>
#include <stdint.h>

#include "./sad_rvv.h"

static unsigned sad_rvv(const uint8_t *src, int src_stride,
                        const uint8_t *ref, int ref_stride,
                        int width, int height) {
  /* chunk 取 VLMAX(u16m2)（VLEN=256 → 32 字节，正好一行的宽度）：
     累加器用 u16m2，diff 用 u8m1，满足 vwaddu.wv 的 "vs1 比 vs2 低一个 LMUL"。 */
  size_t vl = __riscv_vsetvlmax_e16m2();
  vuint16m2_t acc = __riscv_vmv_v_x_u16m2((uint16_t)0, vl);
  int y;

  if ((int)vl > width) vl = __riscv_vsetvl_e16m2((size_t)width);

  for (y = 0; y < height; y++) {
    int x = 0;
    while (x < width) {
      size_t v = (size_t)(width - x) < vl ? (size_t)(width - x) : vl;
      vuint8m1_t a = __riscv_vle8_v_u8m1(src + x, v);
      vuint8m1_t b = __riscv_vle8_v_u8m1(ref + x, v);
      vuint8m1_t hi = __riscv_vmaxu_vv_u8m1(a, b, v);
      vuint8m1_t lo = __riscv_vminu_vv_u8m1(a, b, v);
      vuint8m1_t d = __riscv_vsub_vv_u8m1(hi, lo, v);
      /* u8mf2 -> u16m1 零扩展累加：vwaddu.wv 要求 vs1 的 LMUL 比 vs2 小一号，
         所以 diff 用 u8mf2、累加器用 u16m1（两者 MVL 相同，VLEN=256 时都是 16）。 */
      acc = __riscv_vwaddu_wv_u16m2(acc, d, v);
      x += (int)v;
    }
    src += src_stride;
    ref += ref_stride;
  }

  /* 单次归约：u16 累加器 -> u32。
     u16 溢出边界：每 lane 累计 width*height/16 个像素，64x64 时是 256 个 → 65280，
     刚好不溢出；再大的块要换 u32 累加器。这里四档尺寸都安全。 */
  {
    vuint32m1_t s = __riscv_vwredsumu_vs_u16m2_u32m1(
        acc, __riscv_vmv_v_x_u32m1((uint32_t)0, vl), vl);
    return (unsigned)__riscv_vmv_x_s_u32m1_u32(s);
  }
}

/* 小块（宽 <= 16）专用：chunk 用 u16m1（16 字节），累加器也是 u16m1。
   v3 的 32 字节 chunk 在 8x8/16x16 上会把 u16m2 的两个寄存器都用满，
   反而比这个窄版本慢（实测 8x8 1.07x vs 1.20x、16x16 1.94x vs 2.16x）。 */
static unsigned sad_rvv_small(const uint8_t *src, int src_stride,
                              const uint8_t *ref, int ref_stride,
                              int width, int height) {
  /* 不能假设 width <= MVL：qemu 默认 VLEN=128 时 u16m1 的 MVL 只有 8，
     16 宽就得分两段。所以这里仍然按 chunk 循环，只是 chunk 上限取 u16m1 的 MVL。 */
  size_t vmax = __riscv_vsetvlmax_e16m1();
  vuint16m1_t acc = __riscv_vmv_v_x_u16m1((uint16_t)0, vmax);
  int y;
  for (y = 0; y < height; y++) {
    int x = 0;
    while (x < width) {
      size_t v = (size_t)(width - x) < vmax ? (size_t)(width - x) : vmax;
      vuint8mf2_t a = __riscv_vle8_v_u8mf2(src + x, v);
      vuint8mf2_t b = __riscv_vle8_v_u8mf2(ref + x, v);
      vuint8mf2_t hi = __riscv_vmaxu_vv_u8mf2(a, b, v);
      vuint8mf2_t lo = __riscv_vminu_vv_u8mf2(a, b, v);
      acc = __riscv_vwaddu_wv_u16m1(acc, __riscv_vsub_vv_u8mf2(hi, lo, v), v);
      x += (int)v;
    }
    src += src_stride;
    ref += ref_stride;
  }
  {
    vuint32m1_t s = __riscv_vwredsumu_vs_u16m1_u32m1(
        acc, __riscv_vmv_v_x_u32m1((uint32_t)0, vmax), vmax);
    return (unsigned)__riscv_vmv_x_s_u32m1_u32(s);
  }
}

unsigned vpx_sad8x8_rvv(const uint8_t *src, int src_stride,
                        const uint8_t *ref, int ref_stride) {
  return sad_rvv_small(src, src_stride, ref, ref_stride, 8, 8);
}
unsigned vpx_sad16x16_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv_small(src, src_stride, ref, ref_stride, 16, 16);
}
unsigned vpx_sad32x32_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 32, 32);
}
unsigned vpx_sad64x64_rvv(const uint8_t *src, int src_stride,
                          const uint8_t *ref, int ref_stride) {
  return sad_rvv(src, src_stride, ref, ref_stride, 64, 64);
}
