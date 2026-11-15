/* SSE（平方差和）内核 A/B：C 参照 vs RVV
 *
 * C 参照逐字拷贝自 baseline/vpx_dsp/sse.c 的 vpx_sse_c，不改语义。
 * 两侧编同一份本文件，唯一差别是 -DUSE_RVV=1/0（以及是否链接 sse_rvv.c）。
 * 用 #if USE_RVV 而不是 #if defined(USE_RVV)——后者在 -DUSE_RVV=0 时仍为真。
 *
 * 调用：kernel_ssd <16|32|64> <reps> [v]
 * 输出里 ns_per_call 是"每个块"的成本，块内 2000 次重复。
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#include "vpx_dsp/riscv/sse_rvv.h"

static int64_t sse_c(const uint8_t *a, int a_stride, const uint8_t *b,
                     int b_stride, int width, int height) {
  int y, x;
  int64_t sse = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const int32_t diff = abs(a[x] - b[x]);
      sse += diff * diff;
    }
    a += a_stride;
    b += b_stride;
  }
  return sse;
}

typedef int64_t (*sse_fn)(const uint8_t *, int, const uint8_t *, int);
static int64_t c16(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sse_c(s, ss, r, rs, 16, 16); }
static int64_t c32(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sse_c(s, ss, r, rs, 32, 32); }
static int64_t c64(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sse_c(s, ss, r, rs, 64, 64); }

#if USE_RVV
static int64_t r16(const uint8_t *s, int ss, const uint8_t *r, int rs) { return vpx_sse_rvv(s, ss, r, rs, 16, 16); }
static int64_t r32(const uint8_t *s, int ss, const uint8_t *r, int rs) { return vpx_sse_rvv(s, ss, r, rs, 32, 32); }
static int64_t r64(const uint8_t *s, int ss, const uint8_t *r, int rs) { return vpx_sse_rvv(s, ss, r, rs, 64, 64); }
#endif

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static uint32_t st = 98765u;
static uint32_t rnd(void) { st = st * 1664525u + 1013904223u; return st; }

int main(int argc, char **argv) {
  int size = (argc > 1) ? atoi(argv[1]) : 16;   /* 16/32/64 */
  int reps = (argc > 2) ? atoi(argv[2]) : 1;
  int w = size, h = size, sp = w + 8;
  uint8_t *src = malloc((size_t)sp * h), *ref = malloc((size_t)sp * h);
  int i, r;
  sse_fn cs = size == 16 ? c16 : size == 32 ? c32 : c64;
  int64_t expect, got;

  for (i = 0; i < sp * h; i++) {
    src[i] = (uint8_t)(rnd() & 0xFF);
    ref[i] = (uint8_t)(rnd() & 0xFF);
  }

  expect = cs(src, sp, ref, sp);
#if USE_RVV
  {
    sse_fn vs = size == 16 ? r16 : size == 32 ? r32 : r64;
    got = vs(src, sp, ref, sp);
    if (got != expect) {
      printf("ssd%dx%d MISMATCH c=%lld rvv=%lld\n", w, h,
             (long long)expect, (long long)got);
      return 1;
    }
  }
#else
  got = expect;
#endif

  {
    double best = 1e30;
    for (r = 0; r < reps; r++) {
      double t0 = now_ms();
      int64_t acc = 0;
      int k;
#if USE_RVV
      sse_fn vs = size == 16 ? r16 : size == 32 ? r32 : r64;
      for (k = 0; k < 2000; k++) acc += vs(src, sp, ref, sp);
#else
      for (k = 0; k < 2000; k++) acc += cs(src, sp, ref, sp);
#endif
      {
        double t1 = now_ms();
        if (acc == -1) printf("unreachable %lld\n", (long long)acc);
        if (t1 - t0 < best) best = t1 - t0;
      }
    }
    printf("ssd %dx%d %s ns_per_call=%.1f total_ns=%.0f\n", w, h,
#if USE_RVV
           "rvv",
#else
           "c",
#endif
           best * 1e6 / 2000.0, best * 1e6);
  }
  if (argc > 3) printf("value=%lld\n", (long long)got);
  return 0;
}
