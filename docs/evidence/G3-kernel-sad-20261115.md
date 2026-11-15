# libvpx G3 第一个内核：SAD（2026-11-15）

libvpx 的 SSE/NEON 内核有 190+ 文件，全量照搬不现实。先挑**语义最简单、收益可量**的
SAD（逐像素绝对差之和）做第一个 RVV 内核，把"写-验-测"的闭环先跑通。

## 实现

`src/vpx_dsp/riscv/sad_rvv.c`：8x8 / 16x16 / 32x32 / 64x64 四档，语义与上游
`vpx_dsp/sad.c` 的 `static sad()` 完全一致。

关键点：**u8 上不能直接做 `abs(a-b)`**（会回绕）。用

```
hi = vmaxu(a,b); lo = vminu(a,b); d = hi - lo;      // d ∈ [0,255]
total += vwredsumu(d)                                // u8 宽度归约到 u16
```

单行最多 `width*255`（64 宽 = 16320），u16 归约不会溢出；跨行累加到 u32。

## 两个 GCC intrinsic 坑

1. `__riscv_vwredsumu_vs_u8m1_u16` 不存在，正确名是 `..._u8m1_u16m1`；
2. `vwredsumu` 在 GCC 14 的 1.0 intrinsic 里**返回的是 u16m1 向量**（结果在元素 0），
   不是标量，必须再 `__riscv_vmv_x_s_u16m1_u16` 取出来，否则报
   `cannot convert a vector of type 'vuint16m1_t' to type 'unsigned int'`。

## 验证

`test/kernel_sad.c` 里的 C 参照逐字拷贝自上游 `sad()`。两侧同一份文件，
唯一差别 `-DUSE_RVV=1/0`，均 `-O3 -march=rv64gcv -static`，RVV 侧多链一个 `sad_rvv.c`。
RVV 侧同时算 C 与 RVV 并逐值比对（qemu 下四档全部一致）。

## 真机结果（SpacemiT K3）

| 尺寸 | C ns/call | RVV ns/call | 比值 |
|---|---|---|---|
| 8x8 | 70.7 | 96.4 | **0.73×（慢）** |
| 16x16 | 251.4 | 191.0 | **1.32×** |
| 32x32 | 998.6 | 387.9 | **2.57×** |
| 64x64 | 3196.2 | 1510.3 | **2.12×** |

### 结论

- **8x8 反而更慢**：每行只有 8 字节，`vsetvl` + `vmax/vmin/vsub` + 归约的固定开销
  远超 8 个像素的算术量。VP9 的运动搜索里 8x8 用得不少，所以要单独给小块做路径：
  把多行拼成一个向量（8x8 一次只装 2 行）或改用 `vle8` 跨步 + 累积，
  而不是沿用"一行一次归约"的写法。
- **64x64 不如 32x32**：VLEN=256 时 64 字节要拆两个 chunk，每行两次归约；
  改成"整块线性 32 字节流"能省掉一半归约。
- 大块 2–2.6× 的收益说明方向是对的，但要先修小块的负优化才有实际意义。

## 尚未做

- **没有接 `rtcd`**：现在只是内核级 A/B，编码器仍走 C。
  接入要改 `vpx_dsp_rtcd_defs.pl` + 生成器，属于下一步；
- 编码器级 A/B（vpxenc 10 帧）等接入后再测，否则测的是同一个 C 路径。
