#!/usr/bin/env bash
# G3 编码器级 A/B：把 SAD / SSE 内核通过编译期开关 CONFIG_RVV_SAD / CONFIG_RVV_SSE 接到真实编码路径，
# 两个配置只差这一个宏，其余（-march、优化级别、构建参数）完全一致。
#
#   ./scripts/g3-wire-ab.sh            # 编 on/off 两套并在 qemu 下比对哈希
#
# 产物：build/k-rvv/{on,off}/{prefix,harness}
# 纪律：qemu 只判功能一致，性能数字上真机取。
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:2}
MARCH=${MARCH:-rv64gcv}
OUT=build/k-rvv

for side in off on; do
  if [ "$side" = on ]; then
    SADFLAG="-DCONFIG_RVV_SAD=1 -DCONFIG_RVV_SSE=1"
  else
    SADFLAG=""
  fi
  echo "=== 构建 ${side}（额外 CFLAGS: '${SADFLAG:-<无>}'）==="
  docker run --rm -v "$PWD":/work -w /work -e SIDE="$side" -e MARCH="$MARCH" \
    -e SADFLAG="$SADFLAG" -e OUT="$OUT" "${IMAGE}" bash -lc '
set -e
rm -rf "${OUT}/${SIDE}"
mkdir -p "${OUT}/${SIDE}"
cp -R src "${OUT}/${SIDE}/src"
cd "${OUT}/${SIDE}/src"
CC=riscv64-linux-gnu-gcc CXX=riscv64-linux-gnu-g++ AR=riscv64-linux-gnu-ar \
./configure --target=generic-gnu --prefix="/work/${OUT}/${SIDE}/prefix" \
  --disable-shared --enable-static --disable-examples --disable-tools \
  --disable-docs --disable-unit-tests --disable-decode-perf-tests \
  --disable-encode-perf-tests \
  --extra-cflags="-march=${MARCH} -O2 ${SADFLAG}" > /tmp/cfg.log 2>&1 \
  || { echo "configure ${SIDE} FAIL"; tail -20 /tmp/cfg.log; exit 1; }
make -j"$(nproc)" > /tmp/bld.log 2>&1 || { echo "build ${SIDE} FAIL"; tail -25 /tmp/bld.log; exit 1; }
make install > /dev/null
cd /work
cmake -S . -B "${OUT}/${SIDE}/harness" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS="-march=${MARCH}" -DPKG_PREFIX="/work/${OUT}/${SIDE}/prefix" \
  > /tmp/h.log 2>&1 || { tail -20 /tmp/h.log; exit 1; }
cmake --build "${OUT}/${SIDE}/harness" -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }
mkdir -p "${OUT}/${SIDE}/out"
qemu-riscv64 -L /usr/riscv64-linux-gnu "${OUT}/${SIDE}/harness/vpx_harness" \
  "${OUT}/${SIDE}/out/hash.txt" > /dev/null
echo "--- ${SIDE} ---"; cat "${OUT}/${SIDE}/out/hash.txt"
'
done

echo '=== 比对 ==='
if diff -q "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" > /dev/null; then
  echo 'PASS：开/关 RVV SAD 的码流与解码像素完全一致'
else
  echo 'FAIL：两侧哈希不同'
  diff "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" || true
  exit 1
fi
