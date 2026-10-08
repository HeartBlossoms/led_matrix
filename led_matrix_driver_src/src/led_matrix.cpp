#include "led_matrix.hpp"          // 自己的头文件，注意后缀
#include <iostream>
#include <cstring>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <time.h>

extern "C" {
#include "ch347_lib.h"             // 第三方 C 头文件
#include "font5x7.h"               // 字库 C 头文件
}

namespace led_matrix {

// ---------- 硬件配置常量 ----------
static constexpr int SPI_HZ          = 1000000;
static constexpr uint8_t INTENSITY   = 8;
static constexpr bool FIX_SWAP       = false;
static constexpr bool FIX_FLIPX      = true;
static constexpr bool FIX_FLIPY      = false;
static constexpr bool FIX_TRANSPOSE  = false;

// ---------- 工具函数 ----------
static void sleep_us(int us) {
    if (us <= 0) return;
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (long)(us % 1000000) * 1000L;
    while (nanosleep(&ts, &ts) != 0) {}
}

static uint8_t bit_reverse8(uint8_t x) {
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

static void apply_transforms(const uint8_t in[8], uint8_t out[8]) {
    uint8_t tmp[8];
    memcpy(tmp, in, 8);
    if (FIX_FLIPY) {
        for (int r = 0; r < 8; r++) out[r] = tmp[7 - r];
        memcpy(tmp, out, 8);
    }
    if (FIX_FLIPX) {
        for (int r = 0; r < 8; r++) tmp[r] = bit_reverse8(tmp[r]);
    }
    if (FIX_TRANSPOSE) {
        uint8_t tr[8] = {0};
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (tmp[y] & (1u << x)) tr[x] |= (1u << y);
        memcpy(tmp, tr, 8);
    }
    memcpy(out, tmp, 8);
}

// 将字符 c 的点阵填充到 frame（8 字节行数据）
// col_offset: 5 列点阵在 8 列中的起始偏移（0~3），建议居中 1
static void buildCharFrame(char c, uint8_t frame[8], int col_offset) {
    const uint8_t* cols = font5x7_get_cols(c); // 5 字节列数据

    for (int x = 0; x < FONT5X7_WIDTH; ++x) {
        uint8_t col = cols[x];
        int dst_col = col_offset + x;          // 目标列号
        if (dst_col < 0 || dst_col >= 8) continue;

        for (int y = 0; y < 8; ++y) {         // 行 0 为最顶部
            if (col & (1u << y)) {
                frame[y] |= (1u << dst_col);
            }
        }
    }
}


// ---------- PIMPL 实现类（嵌套类） ----------
class LedMatrix::Impl {
public:
    Impl(const char* device_path);
    ~Impl();

    void scrollText(const char* text, int frame_us, ScrollDir dir);
    void displayStaticChars(const char* text, bool keep_alive = true);    

    void clear();
    void fill();

private:
    int dev_fd;
    volatile bool running;
    static Impl* current_impl;
    static void sigHandler(int);

    bool openDevice(const char* device_path);
    bool initSPI();
    bool initMAX7219();
    void max7219_write2_raw(uint8_t reg, uint8_t near_val, uint8_t far_val);
    void max7219_write2_all(uint8_t reg, uint8_t val);
    void flush(const uint8_t left[8], const uint8_t right[8]);

    void buildFrame16x8(const uint8_t *cols, int cols_len, int offset,
                        ScrollDir dir, uint8_t left[8], uint8_t right[8]);
};

LedMatrix::Impl* LedMatrix::Impl::current_impl = nullptr;

// ---------- 构造函数 ----------
LedMatrix::Impl::Impl(const char* device_path) : dev_fd(-1), running(false) {
    if (!openDevice(device_path))
        throw std::runtime_error("Failed to open or initialize device");
}

LedMatrix::Impl::~Impl() {
    try {
        clear();
    } catch (...) {
        // 析构时不再向上抛异常，避免 terminate
    }
    if (dev_fd >= 0) {
        CH347CloseDevice(dev_fd);
        dev_fd = -1;
    }
}

// ---------- 设备打开与初始化 ----------
bool LedMatrix::Impl::openDevice(const char* device_path) {
    std::cout << "[LedMatrix] Opening device: " << device_path << std::endl;

    // 把软链接（如 /dev/led_spi）解析成真实的 hidraw 节点路径。
    // libch347 会根据路径名判断设备类型（hidraw / tty / ch34xpis），
    // 如果直接传 /dev/led_spi 给它，它可能识别不出这是 HID 设备。
    char* resolved = realpath(device_path, nullptr);
    if (!resolved) {
        std::cerr << "[LedMatrix] Cannot resolve path: " << device_path << std::endl;
        return false;
    }
    std::string actual_path(resolved);
    free(resolved);
    std::cout << "[LedMatrix] Resolved to: " << actual_path << std::endl;

    dev_fd = CH347OpenDevice(actual_path.c_str());
    if (dev_fd < 0) {
        std::cerr << "[LedMatrix] Cannot open " << device_path << std::endl;
        return false;
    }
    std::cout << "[LedMatrix] Device opened, fd=" << dev_fd << std::endl;
    if (!initSPI()) {
        std::cerr << "[LedMatrix] SPI init failed" << std::endl;
        return false;
    }
    std::cout << "[LedMatrix] SPI initialized" << std::endl;
    if (!initMAX7219()) {
        std::cerr << "[LedMatrix] MAX7219 init failed" << std::endl;
        return false;
    }
    std::cout << "[LedMatrix] MAX7219 initialized" << std::endl;
    return true;
}

bool LedMatrix::Impl::initSPI() {
    mSpiCfgS cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.iMode       = 0;
    cfg.iByteOrder  = 1;
    cfg.iChipSelect = 0x80;
    std::cout << "[LedMatrix] SPI set frequency " << SPI_HZ << " Hz" << std::endl;
    if (!CH347SPI_SetFrequency(dev_fd, (double)SPI_HZ)) {
        std::cerr << "[LedMatrix] SPI_SetFrequency failed" << std::endl;
        return false;
    }
    std::cout << "[LedMatrix] SPI init config: mode=" << (int)cfg.iMode
              << " cs=0x" << std::hex << (int)cfg.iChipSelect << std::dec << std::endl;
    if (!CH347SPI_Init(dev_fd, &cfg)) {
        std::cerr << "[LedMatrix] SPI_Init failed" << std::endl;
        return false;
    }
    return true;
}

bool LedMatrix::Impl::initMAX7219() {
    std::cout << "[LedMatrix] MAX7219: shutdown=0, scan=7, intensity=" << (int)INTENSITY << std::endl;
    try {
        max7219_write2_all(0x0F, 0x00);  // display test off
        max7219_write2_all(0x0C, 0x01);  // shutdown mode off
        max7219_write2_all(0x09, 0x00);  // decode mode off
        max7219_write2_all(0x0B, 0x07);  // scan limit 0-7
        max7219_write2_all(0x0A, INTENSITY);
    } catch (const std::exception& e) {
        std::cerr << "[LedMatrix] MAX7219 init write failed: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void LedMatrix::Impl::max7219_write2_raw(uint8_t reg, uint8_t near_val, uint8_t far_val) {
    uint8_t buf[4] = { reg, far_val, reg, near_val };
    if (!CH347SPI_WriteRead(dev_fd, false, 0x80, 4, buf)) {
        throw std::runtime_error("SPI write failed (CH347SPI_WriteRead returned false)");
    }
}

void LedMatrix::Impl::max7219_write2_all(uint8_t reg, uint8_t val) {
    max7219_write2_raw(reg, val, val);
}

void LedMatrix::Impl::flush(const uint8_t left[8], const uint8_t right[8]) {
    uint8_t L[8], R[8];
    apply_transforms(left, L);
    apply_transforms(right, R);
    for (int row = 0; row < 8; row++) {
        uint8_t nv = FIX_SWAP ? R[row] : L[row];
        uint8_t fv = FIX_SWAP ? L[row] : R[row];
        max7219_write2_raw((uint8_t)(row + 1), nv, fv);
    }
}

void LedMatrix::Impl::clear() {
    uint8_t z[8] = {0};
    flush(z, z);
}

void LedMatrix::Impl::fill() {
    uint8_t f[8]; memset(f, 0xFF, 8);
    flush(f, f);
}

void LedMatrix::Impl::sigHandler(int) {
    if (current_impl) current_impl->running = false;
}

// ---------- 滚动显示 ----------
void LedMatrix::Impl::scrollText(const char* text, int frame_us, ScrollDir dir) {
    uint8_t cols[1024];
    int cols_len = font5x7_text_to_cols(text, cols, (int)sizeof(cols));

    running = true;
    current_impl = this;
    signal(SIGINT, sigHandler);

    if (dir == ScrollDir::LEFT) {
        // 窗口从左向右扫描：offset 增大，内容向左移动
        for (int off = -16; off <= cols_len && running; ++off) {
            uint8_t L[8], R[8];
            buildFrame16x8(cols, cols_len, off, dir, L, R);
            flush(L, R);
            sleep_us(frame_us);
        }
    } else { // ScrollDir::RIGHT
        // 窗口从右向左扫描：offset 减小，内容向右移动
        for (int off = cols_len; off >= -16 && running; --off) {
            uint8_t L[8], R[8];
            buildFrame16x8(cols, cols_len, off, dir, L, R);
            flush(L, R);
            sleep_us(frame_us);
        }
    }
    clear();
}


void LedMatrix::Impl::displayStaticChars(const char* text, bool keep_alive) {
    uint8_t left[8] = {0};
    uint8_t right[8] = {0};

    if (text) {
        if (text[0] != '\0') {
            buildCharFrame(text[0], left, 1);   // 左边显示第一个字符
        }
        if (text[1] != '\0') {
            buildCharFrame(text[1], right, 1);  // 右边显示第二个字符
        }
    }

    flush(left, right);      // 写入硬件

    if (keep_alive) {
        // 阻塞等待 Ctrl+C
        running = true;
        current_impl = this;
        signal(SIGINT, sigHandler);
        while (running) {
            sleep_us(500 * 1000);
        }
        signal(SIGINT, SIG_DFL);
        clear();
    }
}

void LedMatrix::Impl::buildFrame16x8(const uint8_t *cols, int cols_len, int offset,
                                      ScrollDir dir, uint8_t left[8], uint8_t right[8]) {
    (void)dir; // 滚动方向由 scrollText 中的扫描顺序控制，这里只负责取当前窗口
    memset(left, 0, 8);
    memset(right, 0, 8);
    // cols 已经是从左到右的列流；x=0..15 直接对应 cols[offset..offset+15]，
    // 不再根据方向做 (15-x) 反转，从而保证与静态显示的左右方向一致。
    for (int x = 0; x < 16; x++) {
        int src = offset + x;
        uint8_t col = 0;
        if (src >= 0 && src < cols_len) col = cols[src];
        for (int y = 0; y < 8; y++) {
            if (col & (1u << y)) {
                if (x < 8) left[y]  |= (1u << x);
                else       right[y] |= (1u << (x - 8));
            }
        }
    }
}


// ---------- 公开接口 ----------
LedMatrix::LedMatrix(const char* device_path)
    : pimpl_(std::make_unique<Impl>(device_path)) {}

LedMatrix::~LedMatrix() = default;

void LedMatrix::scrollText(const char* text, int frame_us, ScrollDir dir) {
    pimpl_->scrollText(text, frame_us, dir);
}

void LedMatrix::displayStaticChars(const char* text, bool keep_alive) {
    pimpl_->displayStaticChars(text, keep_alive);
}

void LedMatrix::clear() {
    pimpl_->clear();
}

void LedMatrix::fill() {
    pimpl_->fill();
}

} // namespace led_matrix