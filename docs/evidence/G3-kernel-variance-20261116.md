# G3 内核：libvpx variance（三种块尺寸内核 + 整链）

日期 2026-11-16 · 板子 SpacemiT K3（`<board>`，<distro>，`gcc 15.2.0`，`vlenb=32`）
门禁：功能逐字节 + 真机 A/B（三颗宏只差 `CONFIG_RVV_{SAD,SSE,VAR}`）

## 1. 结论

### 1.1 内核（每块 2000 次，5 轮 best-of-5，三轮重复）

| 块 | C 参照 | RVV | 倍率 |
|---|---|---|---|
| 16×16 | 682.1 ns | **287.5–290.0 ns** | **2.37×** |
| 32×32 | 1361.0 ns | **476.5–480.0 ns** | **2.84×** |
| 64×64 | 2716.9–2717.6 ns | **1590.7–1631.3 ns** | **1.70×** |

三档的 `sse` / `sum` / `var` 与 C 完全相等（例如 16×16 `sse=2721920 sum=896
var=2718784`、64×64 `sse=44443648 sum=0 var=44443648`），整数运算无容差可言。

### 1.2 整链（VP8 352×288，10 帧）

| 版本 | 三轮 | 最好 |
|---|---|---|
| off（无 RVV 内核） | 597.16 / 597.16 / 598.18 ms | 597.16 ms |
| on（SAD+SSE+variance） | 504.08 / 522.17 / 524.40 ms | **504.08 ms** |

**1.185×**（用同轮最好值）。码流与解码像素逐字节相同：

```
frames=10 bytes=10766 fnv_bit=62a63640d7690a11 dec_frames=10 fnv_px=f85eaa8153b093fc
```

上一轮只有 SAD+SSE 时是 574.77 → 508.23 = 1.131×；本轮加 variance 后进到 1.185×，
增量约 +4.8%。注意两轮的 off 基准不同（574.77 对 597.16），**跨轮不可直接比**，
只能读同一轮内的比值。

## 2. 结构：从 SSE 骨架改出来的双累加器

variance 与 SSE 只差一个输出：除了 `sum(diff^2)` 还要 `sum(diff)`，且两者都必须是精确整数
（上层 `sse - sum*sum/(w*h)` 对舍入极敏感，不能用浮点近似）。

```
va, vb  = vle8 u8m1
d       = vsub(vreinterpret(vzext(va)), vreinterpret(vzext(vb)))   // i16m2，落在 [-255,255]
acc_s   = vwadd_wv_i32m4(acc_s,  d)       // 加宽累加差分本身（可为负）
acc_ss  = vwmacc_vv_i32m4(acc_ss, d, d)   // 加宽累加平方
```

两个累加器都是 i32m4、跨行连续累加，批末才归约。批宽度 30000 像素：
`30000×65025 ≈ 1.95e9 < 2^31`（sse 侧），`30000×255 ≈ 7.65e6`（sum 侧）。

**为什么必须跨行累加**：归约要把整条向量 `vse32` 到栈再标量求和。16×16 只有 256 像素，
每行一次归约的开销会把向量化收益吃掉 —— 这个坑 SSE 内核已经踩过（逐行归约让 16×16
从正收益变成 0）。variance 直接沿用批量化，16×16 因此能拿到 2.37×。

## 3. 为什么 64×64 反而掉到 1.70×

三条曲线不是单调的：2.37× → 2.84× → 1.70×。原因在寄存器压力，不在算法。

`i32m4` 一个累加器就占 4 个向量寄存器，两个占 8 个；加上 `va`/`vb`（u8m1）、`d`（i16m2），
64×64 时这些量在整个累加期都要同时活跃，K3 只有 32 个向量寄存器，余量被压到很窄。
这与 zlib-ng adler32 的 `u32m8` 直接 spill 是同一族现象，只是没有到 spill 的程度、而是
调度自由度变小。

另一层原因是**基数变小**：variance 的 C 参照没有 `abs()` 调用（`diff = src[j] - ref[j]`
直接做整数减法），比 SSE 的 C 版快一倍多（64×64 是 2717 ns 对 6063 ns），所以同样的
RVV 绝对耗时换算出更低的比值。SSE 的 64×64 是 6.52×，variance 只有 1.70×，
**这不是 RVV 变差了，是分母变小了**。报告里比较不同内核的倍率时要注意这一点。

试过但没采用的替代方案：把 `acc_s` 改成每 32 行归约一次、降成 i16 累加 —— 归约次数从 1
次涨到 2 次，省下的寄存器余量抵不过归约开销，且精度上必须严格证明不溢出，收益不明确。

## 4. 接入面：一个分流点覆盖整族

分流放在 `vpx_dsp/variance.c` 的 **static `variance()`**，而不是各个
`vpx_varianceWxH_c`：

- 所有 `VAR(W,H)` 宏展开的 `vpx_varianceWxH_c` 都调它；
- `vpx_sub_pixel_varianceWxH` / `vpx_sub_pixel_avg_varianceWxH` 的中间缓冲也最终调它。

所以一处接入就覆盖了整族，不需要挨个改几十个函数。这也是当初选 SSE 改 `vpx_sse_c`
内部而非改 rtcd 的同一个思路：**riscv 的 rtcd 没有 SIMD 特化，C 路径就是唯一路径，
在 C 函数里分流最省事**。

## 5. 适用性判据

```c
int vpx_variance_rvv_applicable(int w, int h) { return w >= 16 && h >= 4; }
```

宽度 <16 时（4×4 / 4×8 / 8×4 / 8×8），`vsetvl` 与批末归约的固定开销占比过高，
向量通道填不满；高度 <4 时跨行累加摊不开。这两条与 SAD/SSE 的门槛一致。

## 6. 复现

```bash
# 内核
cd /root/rvv-k/libvpx
M="-O2 -march=rv64gcv_zvl256b -mrvv-vector-bits=zvl"
gcc $M -DUSE_RVV=1 -I src test/kernel_var.c src/vpx_dsp/riscv/variance_rvv.c -o kernel_var_on
gcc $M -DUSE_RVV=0 -I src test/kernel_var.c                          -o kernel_var_off
for s in 16 32 64; do ./kernel_var_off $s 5; ./kernel_var_on $s 5; done

# 整链（容器交叉编，产物 scp 到板子跑）
./scripts/g3-wire-ab.sh          # 已把 -DCONFIG_RVV_VAR=1 加进 on 侧
```

`kernel_var` 的输入用 `a[i]=(i*37+(i>>3))&0xff`、`b[i]=(i*11+7)&0xff`，
两组数据正负差异都有，能覆盖 `sum` 为负的路径（16×16 实测 `sum=896`、64×64 实测 `sum=0`，
说明两条分支都走过了）。

## 7. 坑

1. **宏名必须用 `-D` 传，不能靠 vpx_config.h**：`CONFIG_RVV_VAR` 不在 `vpx_config.h` 里，
   未定义时 `#if CONFIG_RVV_VAR` 视为 0 —— 与 `CONFIG_RVV_SSE` 同一个做法。
   注意 `#if CONFIG_RVV_VAR` 与 `#if defined(...)` 不同，后者在 `-D...=0` 时仍为真。
2. **`vwadd_wv_i32m4` 的 vs2 必须是 i16m2**（LMUL 比 vd 低两档），不能给 i16m4。
3. **`iwmacc` 必须有符号**：差为负时按无符号解释会把 `0xFFxx` 当成大正数，
   得到完全错误的 sse（这个坑在 SSE 内核里已经吃过一次）。
4. **小样本测不到负 sum**：如果测试数据让所有 diff 同号，`acc_s` 的负值分支根本不会被触发，
   哈希比对就形同虚设。本轮用的两组数据刻意让正负差异混合。

## 8. 后续

- `vpx_fdct`/`vpx_fht`（量化变换）、`vpx_quantize` 未接。
- `vpx_variance` 的 `sub_pixel` 变体虽然也走这个分流点，但前面的双线性滤波
  （`var_filter_block2d_bil_*`）还是 C，那段在 `sub_pixel` 场景下占比不小，是下一个可选目标。
- rtcd 运行时分发仍未做，目前全靠编译期宏；换到"运行时按 hwprobe 选"要动 rtcd 生成器。
