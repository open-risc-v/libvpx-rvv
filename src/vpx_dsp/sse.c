/*
 *  Copyright (c) 2023 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 * Sum the square of the difference between every corresponding element of the
 * buffers.
 */

#include <stdlib.h>

#include "./vpx_config.h"
#include "./vpx_dsp_rtcd.h"

#include "vpx/vpx_integer.h"
#include "vpx_ports/mem.h"

#if CONFIG_RVV_SSE
/* G3：编译期接线，与 SAD 同一套纪律（A/B 的唯一变量就是这个宏）。
   只能在 vpx_sse_c 里分流：rtcd 在 riscv 上没有 SIMD 特化，vpx_sse 就是落到 vpx_sse_c。 */
#include "riscv/sse_rvv.h"
#include "riscv/sse_rvv.c"
#endif

int64_t vpx_sse_c(const uint8_t *a, int a_stride, const uint8_t *b,
                  int b_stride, int width, int height) {
  int y, x;
  int64_t sse = 0;

#if CONFIG_RVV_SSE
  if (vpx_sse_rvv_applicable(width, height))
    return vpx_sse_rvv(a, a_stride, b, b_stride, width, height);
#endif

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

#if CONFIG_VP9_HIGHBITDEPTH
int64_t vpx_highbd_sse_c(const uint8_t *a8, int a_stride, const uint8_t *b8,
                         int b_stride, int width, int height) {
  int y, x;
  int64_t sse = 0;
  uint16_t *a = CONVERT_TO_SHORTPTR(a8);
  uint16_t *b = CONVERT_TO_SHORTPTR(b8);
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      const int32_t diff = (int32_t)(a[x]) - (int32_t)(b[x]);
      sse += diff * diff;
    }

    a += a_stride;
    b += b_stride;
  }
  return sse;
}
#endif
