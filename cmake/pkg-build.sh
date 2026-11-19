#!/usr/bin/env bash
# 构建上游 libvpx（autoconf 风格 configure）→ <out>/prefix。
# 两侧都显式 --target=generic-gnu：libvpx 没有 riscv 目标，而且 generic 是纯 C 路径，
# 与 riscv 侧同源，A/B 只差架构本身。configure 会写源码树，所以复制一份再编。
set -euo pipefail
SRC=$1
OUT=$2
MARCH=${3:-rv64gcv}
KERN=${4:-0}
EXTRA=${EXTRA_CFLAGS:-}
if [ "$KERN" = "1" ]; then EXTRA="$EXTRA -march=${MARCH} -DCONFIG_RVV_SAD=1 -DCONFIG_RVV_SSE=1"; fi
mkdir -p "$OUT"
rm -rf "$OUT/src" && cp -R "$SRC" "$OUT/src"
cd "$OUT/src"
env -u CFLAGS -u CXXFLAGS -u LDFLAGS ./configure \
  --target=generic-gnu --prefix="$OUT/prefix" \
  --disable-shared --enable-static \
  --disable-examples --disable-tools --disable-docs --disable-unit-tests \
  --disable-decode-perf-tests --disable-encode-perf-tests \
  ${EXTRA:+--extra-cflags="$EXTRA"} > "$OUT/configure.log" 2>&1 || { tail -30 "$OUT/configure.log"; exit 1; }
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
make install > "$OUT/install.log" 2>&1 || { tail -20 "$OUT/install.log"; exit 1; }
