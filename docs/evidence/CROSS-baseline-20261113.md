# 交叉编译基线（riscv64）— libvpx

## 口径
- 只编 baseline，`MARCH=rv64gc`，镜像 `rvv-cross:2`（riscv64-linux-gnu-gcc/g++ + qemu）
- **`--target=generic-gnu`**：libvpx 的 configure 里没有 riscv 目标（只有 x86/arm/mips/ppc），
  generic 是纯 C 路径，native 侧也用同一 target，A/B 只差架构
- 探针 `test/pkg_vpx.c`：VP8 128×96 ×10 帧，CBR 200 kbps、cpu-used=0（关速度特性）、
  单线程、`VP8E_SET_TOKEN_PARTITIONS=0`；码流与解码 I420 一起哈希

## 结果
| 侧 | 输出 |
|---|---|
| riscv64 | `frames=10 bytes=10766 fnv_bit=62a63640d7690a11 dec_frames=10 fnv_px=f85eaa8153b093fc` |
| native | 同左 |

**全字段一致**。踩坑：`pkt->data.frame.buf` 在新版本是 `void*`，C 里不能直接下标，要先转 `uint8_t*`。

## 复现
```bash
cd ~/work/libvpx-rvv
./test/run_ab.sh && ./scripts/cross-baseline.sh
```
