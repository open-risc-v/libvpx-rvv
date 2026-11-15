# libvpx SSE（平方差和）RVV 内核 + 整链 A/B

- 日期：2026-11-16
- 上游：`webmproject/libvpx` @ `6f4dbb1bc6803cbcdb7f54bd1c11a6228b717705`
- 编译：`--target=generic-gnu`，静态，两侧同 `-march=rv64gcv -O2`；唯一变量是
  `-DCONFIG_RVV_SAD=1 -DCONFIG_RVV_SSE=1`
- 真机：SpacemiT K3 / Bianbu `Linux k3 6.18.3 riscv64`

## 1. 为什么接 SSE 而不是继续抠 SAD

SAD 那轮（§4.1 报告 / 内核 4.28×、整链 1.12×）之后，libvpx 里同族的整数内核就剩
`vpx_sse`（平方差和）——它在 RD 代价、帧内/帧间模式选择里被反复调用。
它比 SAD 更"纯净"：`int64_t vpx_sse_c(a, a_stride, b, b_stride, width, height)`
是一个通用宽高的函数，没有 rtcd 的尺寸分发表要改。

## 2. 内核

```
u8 × 2 → vzext u16 → 相减 i16（差在 [-255,255]）
       → vwmacc_vv_i32m4(d, d) 加宽累加
批末：vse32 到栈上 + 标量求和（每 30000 像素一批）
```

三个要点：

1. **必须用有符号的 `vwmacc`**。差可以是负数，先 `vreinterpret u16→i16` 再
   `vwmacc_vv_i32m4`；用无符号版会把 `0xFFxx` 当成大正数，结果错得离谱。
2. **归约必须批量化**。一开始逐行归约（存栈求和），16×16 时开销直接等于 100%——
   每行 16 个乘加配 16 个标量加。改成"i32 车道跨行累加、每 30000 像素归约一次"后，
   单像素最大贡献 255²=65025，30000×65025≈1.95e9 < 2³¹，安全。
3. **GCC 14.2 没有 u32 源的加宽归约**。`vwredsumu_vs_u16m2_u32m1`（u16 源）是有的
   （SAD 内核就在用），但 u32 源 → u64 的 `vwredsumu_vs_u64m1_u32m2` /
   `vs_u64m2_u32m4` / `vredsumu_vs_u32m1_u32m4` 全都 "implicit declaration"。
   不赌名字，批末存栈再标量求和；摊到 30000 像素上成本可忽略。

接线：`CONFIG_RVV_SSE` 打开时在 `vpx_sse_c` 开头分流（riscv 没有 SIMD 特化，
rtcd 的 `vpx_sse` 本来就落到 `vpx_sse_c`）。宽度 <16 或高度 <8 回落 C。

## 3. 功能

qemu 下 `kernel_ssd`（off/on）与 `vpx_harness`（off/on）哈希完全一致：

```
frames=10 bytes=10766 fnv_bit=62a63640d7690a11 dec_frames=10 fnv_px=f85eaa8153b093fc
```

## 4. 真机性能

### 4.1 内核（`kernel_ssd <16|32|64> <reps>`，每轮 2000 次取最优）

| 块 | C | RVV | 比值 |
|---|---|---|---|
| 16×16 | 403.3–405.1 ns | **134.7 ns** | **3.00×** |
| 32×32 | 1252.4–1252.6 ns | **256.8 ns** | **4.88×** |
| 64×64 | 6063.2–6087.7 ns | **932.1 ns** | **6.52×** |

### 4.2 整链（VP8 编码 128×96 / 10 帧，5 轮 × 每侧 3 次取最优）

| 侧 | 时间 |
|---|---|
| off（纯 C） | 574.77–576.10 ms |
| on（SAD + SSE 都走 RVV） | **508.23–509.15 ms** |

**1.131×**。对比上一轮只有 SAD 时的 ≈1.12×，SSE 再贡献约 +1%。

## 5. 诚实的结论

内核 3.00×/4.88×/6.52× 是真的，但整链只 +1%：128×96 的 10 帧编码里，
`vpx_sse` 的调用量远小于 SAD，而且编码器主循环的其它部分（量化、变换、熵编码、
模式搜索里的 RD 比较）仍然是 C。这条路的上限不在内核，在"接了几个内核"。
要再往上走，得继续接 `vpx_variance*`（它内部也调 SSE）、`vpx_fdct/fht`、量化。

## 6. 复现

```bash
./scripts/g3-wire-ab.sh                     # off/on 两套 + qemu 哈希比对
# 内核单测
docker run --rm -v "$PWD":/work -w /work/src rvv-cross:2 bash -lc '
  riscv64-linux-gnu-gcc -march=rv64gcv -O2 -I. -c vpx_dsp/riscv/sse_rvv.c -o /tmp/s.o
  riscv64-linux-gnu-gcc -march=rv64gcv -O2 -I. -DUSE_RVV=1 ../test/kernel_ssd.c /tmp/s.o -static -o /tmp/kss
'
# 真机
kernel_ssd_off|on <16|32|64> <reps> [v]
enc_off|on <hash 输出路径>                  # 计时在 stderr 的 enc_ms
```
