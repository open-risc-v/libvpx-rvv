/* variance 内核 A/B：C 参照 vs RVV
 *
 * C 参照逐字拷贝自 baseline/vpx_dsp/variance.c 的 static variance()，不改语义。
 * 两侧编同一份本文件，唯一差别是 -DUSE_RVV=1/0（以及是否编译 variance_rvv.c）。
 * 用 #if USE_RVV 而不是 #if defined(USE_RVV)——后者在 -DUSE_RVV=0 时仍为真。
 *
 * 调用：kernel_var <16|32|64> <reps> [v]
 * 输出 ns_per_call 是"每个块"成本，块内 2000 次重复；同时打印最终的 variance 值，
 * 两侧必须完全相等（整数，不存在容差）。
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vpx_dsp/riscv/variance_rvv.h"

static void variance_c(const uint8_t *src_ptr, int src_stride,
                       const uint8_t *ref_ptr, int ref_stride, int w, int h,
                       uint32_t *sse, int *sum) {
  int i, j;
  *sum = 0;
  *sse = 0;
  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      const int diff = src_ptr[j] - ref_ptr[j];
      *sum += diff;
      *sse += diff * diff;
    }
    src_ptr += src_stride;
    ref_ptr += ref_stride;
  }
}

/* 与 vpx_dsp/variance.c 的 VAR(W,H) 宏同一个公式 */
static unsigned int var_of(uint32_t sse, int sum, int w, int h) {
  return sse - (unsigned int)(((int64_t)sum * sum) / (w * h));
}

#if USE_RVV
#define RUN(S, RS, R, RR, W, H, SSE, SUM) \
  vpx_variance_rvv(S, RS, R, RR, W, H, SSE, SUM)
#else
#define RUN(S, RS, R, RR, W, H, SSE, SUM) \
  variance_c(S, RS, R, RR, W, H, SSE, SUM)
#endif

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
  int size = argc > 1 ? atoi(argv[1]) : 16;
  int reps = argc > 2 ? atoi(argv[2]) : 5;
  int verbose = argc > 3;
  int stride = size + 32;
  int i, r;
  uint8_t *a, *b;
  uint32_t sse = 0;
  int sum = 0;
  unsigned int var = 0;
  double best = 1e18;

  if (size < 4) return 2;
  if (reps < 1) reps = 1;

  a = malloc((size_t)stride * size);
  b = malloc((size_t)stride * size);
  if (!a || !b) return 3;

  for (i = 0; i < stride * size; i++) {
    a[i] = (uint8_t)((i * 37 + (i >> 3)) & 0xff);
    b[i] = (uint8_t)((i * 11 + 7) & 0xff); /* 正负差异都有，才测得到 sum 的负值路径 */
  }

  for (r = 0; r < reps; r++) {
    double t0 = now_ms();
    for (i = 0; i < 2000; i++) {
      RUN(a, stride, b, stride, size, size, &sse, &sum);
      var ^= var_of(sse, sum, size, size);
    }
    double t1 = now_ms();
    if (t1 - t0 < best) best = t1 - t0;
  }

  printf("size=%dx%d ns_per_call=%.1f sse=%u sum=%d var=%u\n", size, size,
         best * 1e6 / 2000.0, sse, sum, var_of(sse, sum, size, size));
  if (verbose) printf("checksum=%u\n", var);

  free(a);
  free(b);
  return 0;
}
