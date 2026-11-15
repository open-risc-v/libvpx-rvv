/*
 * RVV SSE (sum of squared error) 内核 —— G3。
 *
 * 结构：按行处理，行内按 vl 切块。每块把两路 u8 零扩展到 u16、相减得 i16 差、
 * 再用有符号 vwmacc 把 d*d 加宽累加到 i32。
 *
 * 归约不逐行做：i32 车道累加可以跨行继续加，只要不超过 2^31。
 * 单像素最大贡献 255^2 = 65025，所以每批最多 2^31/65025 ≈ 33000 像素，
 * 这里取 30000 像素为一批，批末把向量存到栈上再标量求和。
 * 逐行归约在 16x16 这种小块上会把开销做到 100%，必须批量化。
 *
 * 判据：与 vpx_sse_c 逐位相同（纯整数，无浮点舍入）。
 */
#include <stdint.h>
#include <riscv_vector.h>

#include "vpx_dsp/riscv/sse_rvv.h"

#if defined(__riscv_vector)

#define SSE_FLUSH_PIXELS 30000

int64_t vpx_sse_rvv(const uint8_t *a, int a_stride, const uint8_t *b,
                    int b_stride, int width, int height) {
  uint64_t total = 0;
  int rows_per_flush;
  int rows_since = 0;
  size_t vlmax;
  vint32m4_t acc;
  int y, x;
  int i;

  if (width <= 0 || height <= 0) return 0;

  vlmax = __riscv_vsetvl_e8m1((size_t)width);
  acc = __riscv_vmv_v_x_i32m4(0, vlmax);
  rows_per_flush = SSE_FLUSH_PIXELS / width;
  if (rows_per_flush < 1) rows_per_flush = 1;

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; x += (int)vlmax) {
      size_t vl = __riscv_vsetvl_e8m1((size_t)(width - x));
      vuint8m1_t va = __riscv_vle8_v_u8m1(a + x, vl);
      vuint8m1_t vb = __riscv_vle8_v_u8m1(b + x, vl);
      /* u8 → u16 → 视作 i16，再相减：差落在 [-255, 255]，不会绕回 */
      vint16m2_t da =
          __riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(va, vl));
      vint16m2_t db =
          __riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(vb, vl));
      vint16m2_t d = __riscv_vsub_vv_i16m2(da, db, vl);

      /* 必须用有符号 vwmacc：差是负的，按无符号解释会把 0xFFxx 当成大正数 */
      acc = __riscv_vwmacc_vv_i32m4(acc, d, d, vl);
    }

    a += a_stride;
    b += b_stride;
    ++rows_since;

    if (rows_since == rows_per_flush || y == height - 1) {
      int32_t tmp[64];
      size_t n = vlmax < 64 ? vlmax : 64;

      /* GCC 14.2 这套 intrinsics 对 u32 源的归约只开放到 m1 形状，
         u32m4 → u64 的加宽归约编不过（u16m2→u32m1 是有的）。
         批末存栈再标量求和，成本摊到几万像素上可以忽略。 */
      __riscv_vse32_v_i32m4(tmp, acc, n);
      for (i = 0; i < (int)n; ++i) total += (uint64_t)(int64_t)tmp[i];
      acc = __riscv_vmv_v_x_i32m4(0, vlmax);
      rows_since = 0;
    }
  }

  return (int64_t)total;
}

/* 小宽度（4/8）时 vsetvl + 批末归约的开销会盖掉收益，交回 C。 */
int vpx_sse_rvv_applicable(int width, int height) {
  return width >= 16 && height >= 8;
}

#else /* 没有 RVV 时仍可编译（off 侧走 C 分支，不会调用到这里） */

int64_t vpx_sse_rvv(const uint8_t *a, int a_stride, const uint8_t *b,
                    int b_stride, int width, int height) {
  (void)a; (void)a_stride; (void)b; (void)b_stride; (void)width; (void)height;
  return 0;
}

int vpx_sse_rvv_applicable(int width, int height) {
  (void)width; (void)height;
  return 0;
}

#endif /* __riscv_vector */
