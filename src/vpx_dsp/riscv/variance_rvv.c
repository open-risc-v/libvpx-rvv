/*
 * RVV variance 内核 —— G3。
 *
 * variance 比 SSE 多一条输出：除了 sum(diff^2) 还要 sum(diff)，两者都必须是
 * 精确整数（上层 `sse - sum*sum/(w*h)` 对舍入极敏感），所以这里不能用浮点跨度近似。
 *
 * 结构：与 sse_rvv.c 同骨架——两路 u8 零扩到 u16、相减得 i16 差，然后
 *   acc_s  = vwadd_wv_i32m4(acc_s,  d)      // 加宽累加差分本身（可负）
 *   acc_ss = vwmacc_vv_i32m4(acc_ss, d, d)  // 加宽累加平方
 * 两个累加器都是 i32、跨行连续累加，批末才归约一次。
 *
 * 为什么必须跨行累加、而不是每行归约：归约要把整条向量存到栈再标量求和，
 * 16x16 只有 256 像素时，每行一次归约的开销会盖过向量化收益（SSE 内核踩过这个坑，
 * 逐行归约让 16x16 的正收益变成 0）。
 *
 * 溢出界：单像素 |diff| <= 255，diff^2 <= 65025。
 * 取 30000 像素一批：30000*65025 ≈ 1.95e9 < 2^31，30000*255 ≈ 7.65e6 也远小于界。
 *
 * 判据：与 C 参照逐位相同（纯整数运算，不存在误差容忍问题）。
 */
#include <stdint.h>
#include <riscv_vector.h>

#include "vpx_dsp/riscv/variance_rvv.h"

#if defined(__riscv_vector)

#define VAR_FLUSH_PIXELS 30000

/* 窄块（4x4/4x8/8x4/8x8）在 16 字节以下的宽度上，vsetvl 与归约的固定开销占比过高；
   高度小于 4 时跨行累加也摊不开。这两条都是实测出来的门槛，与 SAD/SSE 的取舍一致。 */
int vpx_variance_rvv_applicable(int w, int h) { return w >= 16 && h >= 4; }

void vpx_variance_rvv(const uint8_t *src, int src_stride, const uint8_t *ref,
                      int ref_stride, int w, int h, uint32_t *sse, int *sum) {
  int64_t tss = 0;
  int64_t tsum = 0;
  int rows_per_flush;
  int rows_since = 0;
  size_t vlmax;
  vint32m4_t acc_ss, acc_s;
  int32_t tmp_ss[64], tmp_s[64];
  int y, x, i;

  vlmax = __riscv_vsetvl_e8m1((size_t)w);
  acc_ss = __riscv_vmv_v_x_i32m4(0, vlmax);
  acc_s = __riscv_vmv_v_x_i32m4(0, vlmax);

  rows_per_flush = VAR_FLUSH_PIXELS / w;
  if (rows_per_flush < 1) rows_per_flush = 1;

  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; x += (int)vlmax) {
      size_t vl = __riscv_vsetvl_e8m1((size_t)(w - x));
      vuint8m1_t va = __riscv_vle8_v_u8m1(src + x, vl);
      vuint8m1_t vb = __riscv_vle8_v_u8m1(ref + x, vl);
      /* u8 -> u16 -> 当作 i16 再相减：差落在 [-255, 255]，不会无符号绕回 */
      vint16m2_t da =
          __riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(va, vl));
      vint16m2_t db =
          __riscv_vreinterpret_v_u16m2_i16m2(__riscv_vzext_vf2_u16m2(vb, vl));
      vint16m2_t d = __riscv_vsub_vv_i16m2(da, db, vl);

      acc_s = __riscv_vwadd_wv_i32m4(acc_s, d, vl);
      /* 必须用有符号 vwmacc：差为负时按无符号解释会把 0xFFxx 当成大正数 */
      acc_ss = __riscv_vwmacc_vv_i32m4(acc_ss, d, d, vl);
    }

    src += src_stride;
    ref += ref_stride;
    ++rows_since;

    if (rows_since == rows_per_flush || y == h - 1) {
      __riscv_vse32_v_i32m4(tmp_ss, acc_ss, vlmax);
      __riscv_vse32_v_i32m4(tmp_s, acc_s, vlmax);
      for (i = 0; i < (int)vlmax; ++i) {
        tss += tmp_ss[i];
        tsum += tmp_s[i];
      }
      acc_ss = __riscv_vmv_v_x_i32m4(0, vlmax);
      acc_s = __riscv_vmv_v_x_i32m4(0, vlmax);
      rows_since = 0;
    }
  }

  *sse = (uint32_t)tss;
  *sum = (int)tsum;
}

#endif // __riscv_vector
