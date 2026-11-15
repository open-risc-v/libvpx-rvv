/* SAD 内核 A/B：C 参照 vs RVV
 *
 * C 参照逐字拷贝自 baseline/vpx_dsp/sad.c 的 static sad()，不改语义。
 * 两侧编同一份本文件，唯一差别是 -DUSE_RVV=1/0（以及是否链接 sad_rvv.c）。
注意：用 #if USE_RVV 而不是 #if defined(USE_RVV)——后者在 -DUSE_RVV=0 时仍为真。
 * 带 -DCHECK_BOTH=1 时同时算两个实现并逐次比对，用来证明"RVV 没算错"。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "vpx_dsp/riscv/sad_rvv.h"

static unsigned sad_c(const uint8_t *src_ptr, int src_stride,
                      const uint8_t *ref_ptr, int ref_stride,
                      int width, int height) {
  int y, x;
  unsigned int sad = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) sad += abs(src_ptr[x] - ref_ptr[x]);
    src_ptr += src_stride;
    ref_ptr += ref_stride;
  }
  return sad;
}

typedef unsigned (*sad_fn)(const uint8_t *, int, const uint8_t *, int);
static unsigned c8(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sad_c(s, ss, r, rs, 8, 8); }
static unsigned c16(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sad_c(s, ss, r, rs, 16, 16); }
static unsigned c32(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sad_c(s, ss, r, rs, 32, 32); }
static unsigned c64(const uint8_t *s, int ss, const uint8_t *r, int rs) { return sad_c(s, ss, r, rs, 64, 64); }

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static uint32_t st = 12345u;
static uint32_t rnd(void) { st = st * 1664525u + 1013904223u; return st; }

int main(int argc, char **argv) {
  int size = (argc > 1) ? atoi(argv[1]) : 32;   /* 8/16/32/64 */
  int reps = (argc > 2) ? atoi(argv[2]) : 1;    /* 每轮次数，取最优 */
  int w = size, h = size, sp = w + 8;
  uint8_t *src = malloc((size_t)sp * h), *ref = malloc((size_t)sp * h);
  int i, r;
  sad_fn cs = size == 8 ? c8 : size == 16 ? c16 : size == 32 ? c32 : c64;
  unsigned expect, got;
#if USE_RVV
  sad_fn vs = size == 8 ? vpx_sad8x8_rvv : size == 16 ? vpx_sad16x16_rvv
             : size == 32 ? vpx_sad32x32_rvv : vpx_sad64x64_rvv;
#endif
  for (i = 0; i < sp * h; i++) { src[i] = (uint8_t)(rnd() & 0xFF); ref[i] = (uint8_t)(rnd() & 0xFF); }

  expect = cs(src, sp, ref, sp);
#if USE_RVV
  got = vs(src, sp, ref, sp);
  if (got != expect) { printf("sad%dx%d MISMATCH c=%u rvv=%u\n", w, h, expect, got); return 1; }
#else
  got = expect;
#endif

  {
    double best = 1e30;
    for (r = 0; r < reps; r++) {
      double t0 = now_ms();
      unsigned acc = 0;
      int k;
#if USE_RVV
      for (k = 0; k < 2000; k++) acc += vs(src, sp, ref, sp);
#else
      for (k = 0; k < 2000; k++) acc += cs(src, sp, ref, sp);
#endif
      double t1 = now_ms();
      if (acc == 0xFFFFFFFFu) printf("unreachable %u\n", acc);
      if (t1 - t0 < best) best = t1 - t0;
    }
    printf("sad %dx%d %s ns_per_call=%.1f total_ns=%.0f\n", w, h,
#if USE_RVV
           "rvv",
#else
           "c",
#endif
           best * 1e6 / 2000.0, best * 1e6);
  }
  if (argc > 3) printf("value=%u\n", got);
  return 0;
}
