#!/usr/bin/env bash
# 真实包用例：libvpx VP8 编码 10 帧 → 解码回环，码流与解码像素都要有哈希。
set -euo pipefail
: "${STAGE:=run}" "${SIDE:=A}" "${BIN:=build/native}" "${DATA:=build/ab-data}" "${OUT:=build/ab-data/out}"
mkdir -p "$DATA" "$OUT"
case "$STAGE" in
  gen) ;;
  run)
    [ -x "$BIN/vpx_harness" ] || { echo "缺少 $BIN/vpx_harness" >&2; exit 1; }
    "$BIN/vpx_harness" "$OUT/hash.txt" > /dev/null
    grep -q "fnv_px=" "$OUT/hash.txt" || { echo "探针未产出" >&2; exit 1; }
    ;;
  *) echo "未知 STAGE=$STAGE" >&2; exit 2 ;;
esac
