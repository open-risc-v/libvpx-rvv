#!/usr/bin/env bash
# 交叉编译基线版本：libvpx 没有 riscv 目标，用 --target=generic-gnu（纯 C 路径）交叉编，
# 探针用 qemu-user 跑，输出与 native 基线逐字段比对。
#
#   ./scripts/cross-baseline.sh                # rv64gc（无向量）
#   MARCH=rv64gcv ./scripts/cross-baseline.sh  # 上游自带 RVV 代码时再看向量路径
#
# 纪律：只编 baseline，不编 src，不开 RVV_KERNELS；性能数字不从这里取。
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:2}
MARCH=${MARCH:-rv64gc}
OUT=build/riscv64-b

docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc "
set -e
rm -rf ${OUT}
mkdir -p ${OUT} && cp -R baseline ${OUT}/src && rm -rf ${OUT}/src/.git
cd ${OUT}/src
CC=riscv64-linux-gnu-gcc CXX=riscv64-linux-gnu-g++ AR=riscv64-linux-gnu-ar LD=riscv64-linux-gnu-ld \
./configure --target=generic-gnu --prefix=/work/${OUT}/prefix \
  --disable-shared --enable-static \
  --disable-examples --disable-tools --disable-docs --disable-unit-tests \
  --disable-decode-perf-tests --disable-encode-perf-tests \
  --extra-cflags='-march=${MARCH} -O2' > /tmp/vpx-cfg.log 2>&1 || { tail -30 /tmp/vpx-cfg.log; exit 1; }
make -j\$(nproc) > /tmp/vpx-bld.log 2>&1 || { tail -20 /tmp/vpx-bld.log; exit 1; }
make install > /dev/null

cd /work
cmake -S . -B ${OUT}/harness -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS='-march=${MARCH}' \
  -DPKG_PREFIX=/work/${OUT}/prefix > /tmp/h.log 2>&1 || { tail -30 /tmp/h.log; exit 1; }
cmake --build ${OUT}/harness -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }

mkdir -p ${OUT}/out
qemu-riscv64 -L /usr/riscv64-linux-gnu ${OUT}/harness/vpx_harness ${OUT}/out/hash.txt > ${OUT}/out/stdout.txt
echo "--- riscv64（${MARCH}）---"; cat ${OUT}/out/stdout.txt
echo "--- native 基线 ---"; cat build/ab-data/out/01_vpx_roundtrip/a/hash.txt
"
