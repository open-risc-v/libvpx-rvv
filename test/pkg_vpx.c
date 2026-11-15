// libvpx A/B 探针：VP8 编码 10 帧（128×96，CBR，cpu-used=0 关掉速度特性）→ 解码回环。
// 哈希码流字节与解码 I420 平面，两侧必须逐字节一致。
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>

#define W 128
#define H 96
#define NFRAMES 10

static uint64_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

/* 确定性 I420：渐变 + 棋盘 + 噪声，覆盖预测/变换分支 */
static void fill_i420(uint8_t *y, uint8_t *u, uint8_t *v, int f)
{
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++) {
            unsigned int n = (unsigned int)(c * 2654435761u + r * 40503u + f * 7919u);
            n ^= n >> 13;
            int val = (c * 2 + r + f * 3) & 0xff;
            if (((c >> 3) ^ (r >> 3)) & 1) val = 255 - val;
            val ^= (int)(n & 0x0f);
            y[r * W + c] = (uint8_t)val;
        }
    for (int r = 0; r < H / 2; r++)
        for (int c = 0; c < W / 2; c++) {
            u[r * (W / 2) + c] = (uint8_t)((c * 3 + r + f) & 0xff);
            v[r * (W / 2) + c] = (uint8_t)((c + r * 5 + f * 2) & 0xff);
        }
}

int main(int argc, char **argv)
{
    const char *outp = argc > 1 ? argv[1] : NULL;
    vpx_codec_ctx_t enc, dec;
    vpx_codec_enc_cfg_t cfg;
    vpx_codec_dec_cfg_t dcfg;
    vpx_codec_iface_t *iface = vpx_codec_vp8_cx();
    vpx_codec_iface_t *diface = vpx_codec_vp8_dx();

    if (vpx_codec_enc_config_default(iface, &cfg, 0) != VPX_CODEC_OK) return 2;
    cfg.g_w = W; cfg.g_h = H;
    cfg.g_timebase.num = 1; cfg.g_timebase.den = 30;
    cfg.rc_end_usage = VPX_CBR;
    cfg.rc_target_bitrate = 200;
    cfg.g_threads = 1;
    cfg.g_lag_in_frames = 0;
    cfg.g_profile = 0;

    if (vpx_codec_enc_init(&enc, iface, &cfg, 0) != VPX_CODEC_OK) {
        fprintf(stderr, "enc init: %s\n", vpx_codec_error(&enc)); return 3;
    }
    vpx_codec_control(&enc, VP8E_SET_CPUUSED, 0);      /* 关速度特性，保确定性 */
    vpx_codec_control(&enc, VP8E_SET_NOISE_SENSITIVITY, 0);
    vpx_codec_control(&enc, VP8E_SET_STATIC_THRESHOLD, 0);
    vpx_codec_control(&enc, VP8E_SET_TOKEN_PARTITIONS, 0);

    dcfg.w = W; dcfg.h = H; dcfg.threads = 1;
    if (vpx_codec_dec_init(&dec, diface, &dcfg, 0) != VPX_CODEC_OK) {
        fprintf(stderr, "dec init: %s\n", vpx_codec_error(&dec)); return 4;
    }

    uint8_t *y = malloc((size_t)W * H);
    uint8_t *u = malloc((size_t)(W / 2) * (H / 2));
    uint8_t *v = malloc((size_t)(W / 2) * (H / 2));
    if (!y || !u || !v) return 5;

    uint64_t hbits = 1469598103934665603ULL;
    uint64_t hpix = 1469598103934665603ULL;
    unsigned long bytes = 0, frames_out = 0;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int f = 0; f < NFRAMES; f++) {
        fill_i420(y, u, v, f);
        vpx_image_t img;
        if (!vpx_img_alloc(&img, VPX_IMG_FMT_I420, W, H, 1)) return 6;
        for (int r = 0; r < H; r++) memcpy(img.planes[VPX_PLANE_Y] + r * img.stride[VPX_PLANE_Y], y + r * W, W);
        for (int p = 1; p < 3; p++)
            for (int r = 0; r < H / 2; r++)
                memcpy(img.planes[p] + r * img.stride[p], (p == 1 ? u : v) + r * (W / 2), W / 2);

        if (vpx_codec_encode(&enc, &img, f, 1, 0, VPX_DL_BEST_QUALITY) != VPX_CODEC_OK) {
            fprintf(stderr, "encode: %s\n", vpx_codec_error(&enc)); return 7;
        }
        const vpx_codec_cx_pkt_t *pkt;
        vpx_codec_iter_t it = NULL;
        while ((pkt = vpx_codec_get_cx_data(&enc, &it)) != NULL) {
            if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
            bytes += pkt->data.frame.sz;
            hbits = (hbits ^ pkt->data.frame.sz) * 1099511628211ULL;
            const uint8_t *b = pkt->data.frame.buf;
            for (size_t i = 0; i < pkt->data.frame.sz; i++) { hbits ^= b[i]; hbits *= 1099511628211ULL; }

            if (vpx_codec_decode(&dec, b, (unsigned)pkt->data.frame.sz, NULL, 0) != VPX_CODEC_OK) {
                fprintf(stderr, "decode: %s\n", vpx_codec_error(&dec)); return 8;
            }
            vpx_image_t *out;
            vpx_codec_iter_t dit = NULL;
            while ((out = vpx_codec_get_frame(&dec, &dit)) != NULL) {
                frames_out++;
                for (int p = 0; p < 3; p++) {
                    int pw = (p == 0) ? W : W / 2, ph = (p == 0) ? H : H / 2;
                    for (int r = 0; r < ph; r++) {
                        const uint8_t *row = out->planes[p] + r * out->stride[p];
                        for (int c = 0; c < pw; c++) { hpix ^= row[c]; hpix *= 1099511628211ULL; }
                    }
                }
            }
        }
        vpx_img_free(&img);
    }
    /* 冲刷编码器 */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    /* 计时走 stderr：stdout 的哈希行要保持两册可比，不能被时间数字污染 */
    fprintf(stderr, "enc_ms=%.2f\n",
            ((double)t1.tv_sec - (double)t0.tv_sec) * 1000.0 +
            ((double)t1.tv_nsec - (double)t0.tv_nsec) / 1e6);

    if (vpx_codec_encode(&enc, NULL, NFRAMES, 1, 0, VPX_DL_BEST_QUALITY) == VPX_CODEC_OK) {
        const vpx_codec_cx_pkt_t *pkt; vpx_codec_iter_t it = NULL;
        while ((pkt = vpx_codec_get_cx_data(&enc, &it)) != NULL) {
            if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
            bytes += pkt->data.frame.sz;
            const uint8_t *fl = (const uint8_t *)pkt->data.frame.buf;
            for (size_t i = 0; i < pkt->data.frame.sz; i++) { hbits ^= fl[i]; hbits *= 1099511628211ULL; }
        }
    }

    char line[256];
    snprintf(line, sizeof line, "frames=%d bytes=%lu fnv_bit=%016llx dec_frames=%lu fnv_px=%016llx\n",
             NFRAMES, bytes, (unsigned long long)hbits, frames_out, (unsigned long long)hpix);
    if (outp) { FILE *fp = fopen(outp, "wb"); if (fp) { fwrite(line, 1, strlen(line), fp); fclose(fp); } }
    fputs(line, stdout);

    vpx_codec_destroy(&enc); vpx_codec_destroy(&dec);
    free(y); free(u); free(v);
    return frames_out >= (unsigned long)NFRAMES ? 0 : 9;
}
