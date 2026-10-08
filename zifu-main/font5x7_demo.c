/*
 * font5x7_demo.c — 字符库演示程序
 *
 * 支持两种模式:
 *   1. 终端模式 (无设备参数): 在终端 ASCII 打印点阵
 *   2. 硬件模式 (传入 /dev/hidrawX): 驱动 2×MAX7219 8×8 LED 矩阵
 *
 * 编译:  make demo
 * 运行:
 *   ./font5x7_demo                             # 终端打印 "anyverse"
 *   ./font5x7_demo --text "Hello"              # 终端打印自定义文字
 *   sudo ./font5x7_demo /dev/hidraw2           # 硬件滚动 "anyverse"
 *   sudo ./font5x7_demo /dev/hidraw2 --text "Hi!" --hz 1000000 --intensity 8
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#include "ch347_lib.h"
#include "font5x7.h"

static void sleep_us(int us)
{
    if (us <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (long)(us % 1000000) * 1000L;
    while (nanosleep(&ts, &ts) != 0) {
        /* retry if interrupted */
    }
}

/* ================================================================
 *  CH347 / MAX7219 硬件驱动 (与 anyverse.c 相同的参数)
 * ================================================================ */
static const int CS_CSD = 0x80;

typedef enum {
    FUNC_UART = 0,
    FUNC_SPI_I2C_GPIO,
    FUNC_JTAG_GPIO,
    FUNC_SPI_I2C_JTAG_GPIO,
} ch347_functype_t;

static struct {
    int  fd;
    CHIP_TYPE chiptype;
    ch347_functype_t functype;
} dev;

static int     g_hz        = 1000000;
static uint8_t g_intensity = 8;

/* 硬件变换参数 (与 anyverse.c 保持一致) */
static const bool FIX_SWAP      = false;
static const bool FIX_FLIPX     = true;
static const bool FIX_FLIPY     = true;
static const bool FIX_TRANSPOSE = false;

typedef enum { DIR_RIGHT = 0, DIR_LEFT = 1 } scroll_dir_t;
static scroll_dir_t g_dir = DIR_LEFT;

/* ---- 设备检测 ---- */
static bool choose_func_from_path(const char *pathname) {
    if (!strstr(pathname, "hidraw")) {
        printf("Use HID mode: pass /dev/hidrawX\n");
        return false;
    }
    struct hidraw_devinfo info;
    if (ioctl(dev.fd, HIDIOCGRAWINFO, &info) < 0) {
        perror("HIDIOCGRAWINFO"); return false;
    }
    if (info.vendor != 0x1a86) { printf("Not WCH VID.\n"); return false; }

    if (info.product == 0x55dc)      dev.chiptype = CHIP_CH347T;
    else if (info.product == 0x55e5) dev.chiptype = CHIP_CH347F;
    else { printf("Not CH347 HID PID.\n"); return false; }

    char phys[256] = {0};
    if (ioctl(dev.fd, HIDIOCGRAWPHYS(256), phys) < 0) {
        perror("HIDIOCGRAWPHYS"); return false;
    }
    if (dev.chiptype == CHIP_CH347T)
        dev.functype = strstr(phys, "input0") ? FUNC_UART : FUNC_SPI_I2C_GPIO;
    else
        dev.functype = (strstr(phys, "input0") || strstr(phys, "input2"))
                       ? FUNC_UART : FUNC_SPI_I2C_GPIO;
    return true;
}

/* ---- SPI 初始化 ---- */
static bool spi_init_max7219(void) {
    mSpiCfgS cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.iMode = 0;
    cfg.iByteOrder = 1;
    cfg.iSpiOutDefaultData = 0x00;
    cfg.iChipSelect = CS_CSD;
    if (!CH347SPI_SetFrequency(dev.fd, (double)g_hz)) return false;
    if (!CH347SPI_Init(dev.fd, &cfg)) return false;
    return true;
}

/* ---- MAX7219 写入 ---- */
static bool max7219_write2_raw(uint8_t reg, uint8_t near_val, uint8_t far_val) {
    uint8_t buf[4] = { reg, far_val, reg, near_val };
    return CH347SPI_WriteRead(dev.fd, false, CS_CSD, 4, buf);
}
static bool max7219_write2_all(uint8_t reg, uint8_t val) {
    return max7219_write2_raw(reg, val, val);
}

static bool max7219_init(void) {
    if (g_intensity > 0x0F) g_intensity = 0x0F;
    if (!max7219_write2_all(0x0F, 0x00)) return false;
    if (!max7219_write2_all(0x0C, 0x01)) return false;
    if (!max7219_write2_all(0x09, 0x00)) return false;
    if (!max7219_write2_all(0x0B, 0x07)) return false;
    if (!max7219_write2_all(0x0A, g_intensity)) return false;
    return true;
}

/* ---- 坐标变换 ---- */
static uint8_t bit_reverse8(uint8_t x) {
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

static void apply_transforms(uint8_t in[8], uint8_t out[8]) {
    uint8_t tmp[8];
    memcpy(tmp, in, 8);
    if (FIX_FLIPY) {
        for (int r = 0; r < 8; r++) out[r] = tmp[7 - r];
        memcpy(tmp, out, 8);
    }
    if (FIX_FLIPX)
        for (int r = 0; r < 8; r++) tmp[r] = bit_reverse8(tmp[r]);
    if (FIX_TRANSPOSE) {
        uint8_t tr[8] = {0};
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (tmp[y] & (1u << x)) tr[x] |= (1u << y);
        memcpy(tmp, tr, 8);
    }
    memcpy(out, tmp, 8);
}

static void flush_two_modules(uint8_t left[8], uint8_t right[8]) {
    uint8_t L[8], R[8];
    apply_transforms(left, L);
    apply_transforms(right, R);
    for (int r = 0; r < 8; r++) {
        uint8_t nv = FIX_SWAP ? R[r] : L[r];
        uint8_t fv = FIX_SWAP ? L[r] : R[r];
        (void)max7219_write2_raw((uint8_t)(r + 1), nv, fv);
    }
}

static void disp_clear(void) {
    uint8_t z[8] = {0};
    flush_two_modules(z, z);
}
static void disp_fill(void) {
    uint8_t f[8]; memset(f, 0xFF, 8);
    flush_two_modules(f, f);
}

/* ================================================================
 *  使用 font5x7 字符库构建 16×8 帧 & 滚动
 * ================================================================ */

/* 从列流构建一帧 16×8 (两个 8×8 模块) */
static void build_frame_16x8(const uint8_t *cols, int cols_len, int offset,
                              uint8_t left[8], uint8_t right[8])
{
    memset(left, 0, 8);
    memset(right, 0, 8);

    for (int x = 0; x < 16; x++) {
        int src = (g_dir == DIR_RIGHT) ? (offset + x) : (offset + (15 - x));
        uint8_t col = 0;
        if (src >= 0 && src < cols_len) col = cols[src];
        for (int y = 0; y < 8; y++) {
            if (col & (1u << y)) {
                if (x < 8) left[y]  |= (uint8_t)(1u << x);
                else       right[y] |= (uint8_t)(1u << (x - 8));
            }
        }
    }
}

/* 硬件滚动显示 (使用 font5x7 字符库) */
static void hw_scroll_text(const char *msg, int frame_us) {
    uint8_t cols[1024];
    int cols_len = font5x7_text_to_cols(msg, cols, (int)sizeof(cols));

    for (int off = -16; off <= cols_len; off++) {
        uint8_t L[8], R[8];
        build_frame_16x8(cols, cols_len, off, L, R);
        flush_two_modules(L, R);
        sleep_us(frame_us);
    }
}

/* ================================================================
 *  终端可视化 (纯打印, 不需要硬件)
 * ================================================================ */

static void print_glyph(char c)
{
    uint8_t rows[FONT5X7_HEIGHT];
    font5x7_get_rows(c, rows);

    printf("  '%c' (0x%02X)\n", (c >= 0x20 && c <= 0x7E) ? c : '?',
           (unsigned char)c);
    printf("  -------\n");
    for (int y = 0; y < FONT5X7_HEIGHT; y++) {
        printf("  ");
        for (int x = 4; x >= 0; x--)
            printf("%s", (rows[y] & (1u << x)) ? "██" : "· ");
        printf("\n");
    }
    printf("\n");
}

static void print_banner(const char *text)
{
    uint8_t cols[1024];
    int ncols = font5x7_text_to_cols(text, cols, (int)sizeof(cols));

    printf("  ┌");
    for (int i = 0; i < ncols; i++) printf("─");
    printf("┐\n");

    for (int y = 0; y < FONT5X7_HEIGHT; y++) {
        printf("  │");
        for (int x = 0; x < ncols; x++)
            printf("%c", (cols[x] & (1u << y)) ? '#' : ' ');
        printf("│\n");
    }

    printf("  └");
    for (int i = 0; i < ncols; i++) printf("─");
    printf("┘\n");
}

static void terminal_demo(const char *msg)
{
    printf("\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║     font5x7 字符库演示 (demo)        ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    printf("字库信息: %d 个字形 (0x%02X ~ 0x%02X)\n\n",
           font5x7_glyph_count(), FONT5X7_FIRST_CHAR, FONT5X7_LAST_CHAR);

    printf("── 单字符点阵 ──\n\n");
    for (const char *p = msg; *p; p++)
        print_glyph(*p);

    printf("── 横幅模式 (列流) ──\n\n");
    printf("  \"%s\"  (宽度 = %d 列)\n\n", msg, font5x7_text_width(msg));
    print_banner(msg);

    printf("\n── 全部大写字母 ──\n\n");
    print_banner("ABCDEFGHIJKLM");
    printf("\n");
    print_banner("NOPQRSTUVWXYZ");

    printf("\n── 全部小写字母 ──\n\n");
    print_banner("abcdefghijklm");
    printf("\n");
    print_banner("nopqrstuvwxyz");

    printf("\n── 数字与符号 ──\n\n");
    print_banner("0123456789");
    printf("\n");
    print_banner("!@#$%^&*()");
    printf("\n");
}

/* ================================================================ */
static void usage(const char *prog)
{
    printf("用法:\n");
    printf("  终端模式:  %s [--text \"message\"]\n", prog);
    printf("  硬件模式:  sudo %s /dev/hidrawX [--text \"msg\"] "
           "[--hz N] [--intensity 0-15] [--dir left|right]\n\n", prog);
}

int main(int argc, char **argv)
{
    const char *devpath = NULL;
    const char *msg     = "12334444";

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--text") && i + 1 < argc) {
            msg = argv[++i];
        } else if (!strcmp(argv[i], "--hz") && i + 1 < argc) {
            g_hz = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--intensity") && i + 1 < argc) {
            g_intensity = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dir") && i + 1 < argc) {
            const char *d = argv[++i];
            if (!strcmp(d, "left"))  g_dir = DIR_LEFT;
            else if (!strcmp(d, "right")) g_dir = DIR_RIGHT;
            else { usage(argv[0]); return 1; }
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        } else if (strstr(argv[i], "/dev/")) {
            devpath = argv[i];
        } else {
            printf("未知参数: %s\n", argv[i]);
            usage(argv[0]); return 1;
        }
    }

    /* ---------- 终端模式 ---------- */
    if (!devpath) {
        terminal_demo(msg);
        return 0;
    }

    /* ---------- 硬件模式 ---------- */
    printf("[hw] 打开设备 %s ...\n", devpath);
    dev.fd = CH347OpenDevice(devpath);
    if (dev.fd < 0) { printf("CH347OpenDevice 失败。\n"); return 1; }

    if (!choose_func_from_path(devpath)) { CH347CloseDevice(dev.fd); return 1; }
    if (dev.functype != FUNC_SPI_I2C_GPIO &&
        dev.functype != FUNC_SPI_I2C_JTAG_GPIO) {
        printf("该 hidraw 不是 SPI/I2C/GPIO 接口，请尝试另一个。\n");
        CH347CloseDevice(dev.fd);
        return 1;
    }

    if (!spi_init_max7219())  { printf("SPI 初始化失败。\n");     CH347CloseDevice(dev.fd); return 1; }
    if (!max7219_init())      { printf("MAX7219 初始化失败。\n"); CH347CloseDevice(dev.fd); return 1; }

    printf("[hw] 使用 font5x7 字符库滚动显示: \"%s\"\n", msg);
    printf("[hw] 按 Ctrl+C 退出\n\n");

    while (1) {
        disp_fill();
        sleep_us(400 * 1000);

        disp_clear();
        sleep_us(250 * 1000);

        hw_scroll_text(msg, 80 * 1000);

        disp_clear();
        sleep_us(350 * 1000);
    }

    CH347CloseDevice(dev.fd);
    return 0;
}
