# 性能与正确性日志 —— `media-libs/libvpx`

每条记录必须包含：**日期 / 变更 / 内核 / 输入规模 / A / B / 比值 / 是否位精确 / 证据路径**。
没有 A/B 双数字的记录不算数。

## 基线（G2，内核关闭）

| 日期 | 构建 | 用例 | 规模 | A(ms) | B(ms) | 备注 |
|---|---|---|---|---|---|---|
|  | riscv64 |  |  |  |  |  |

```
cpu_probe（目标机）：
vlen=
vle_ticks=  vlse_ticks=  vrgather_ticks=  vsetvl_ticks=
```

## 逐轮记录（G3）

| 轮 | 日期 | 内核 | LMUL | 规模 | A(ms) | B(ms) | 收益 | 位精确 | 备注 |
|---|---|---|---|---|---|---|---|---|---|
| 1 |  |  |  |  |  |  |  |  |  |

## 收敛分析（G4）

```
perf top（剩余热点，按占比排序）：
1.
2.
3.
结论：覆盖度 f=____；停止理由：____
```

## 真机环境修复（gcc 15.2 / rv64gcv, <board>）

- 症状：`pkg` 目标失败，`./configure: 17: .: cannot open ./build/make/configure.sh`。
- 根因：`src/` 树缺少上游 `build/make/`（该目录被 `.gitignore` 的 `build/` 规则排除，复制 src 时静默丢失）。
- 处置：`rsync -a --ignore-existing baseline/ src/` 补齐，不改 baseline。
- 结果：A/B `ALL PASS`（`00_framework` 2 文件、`01_vpx_roundtrip` 1 文件逐字节一致）。
- 注意：该目录属被忽略路径，需在 `build.sh` 或 `pkg-build.sh` 里加断言 `test -f "$SRC/build/make/configure.sh"`，防止再次静默丢失。
