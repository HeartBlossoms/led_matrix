#ifndef LED_MATRIX_HPP
#define LED_MATRIX_HPP

#include <cstdint>
#include <memory>

namespace led_matrix {

enum class ScrollDir { LEFT, RIGHT };

class LedMatrix {
public:
    /**
     * @brief 构造函数，自动打开设备并初始化硬件
     * @param device_path 设备节点，默认为 "/dev/led_spi"（udev 规则创建的固定软链接），也可手动传入如 "/dev/hidraw1"
     * @throws std::runtime_error 如果设备打开或初始化失败
     */
    explicit LedMatrix(const char* device_path = "/dev/led_spi");
    ~LedMatrix();

    // 禁止拷贝
    LedMatrix(const LedMatrix&) = delete;
    LedMatrix& operator=(const LedMatrix&) = delete;

    /**
     * @brief 滚动显示 ASCII 文本（阻塞，Ctrl+C 退出）
     * @param text 要显示的字符串（仅支持 5x7 字库可打印字符）
     * @param frame_us 每帧停留时间（微秒），默认 80000（80ms）
     * @param dir 滚动方向
     */
    void scrollText(const char* text, int frame_us = 80000,
                    ScrollDir dir = ScrollDir::LEFT);

    /** 静态显示最多两个字符（左右两块各显示一个）
     * @param text   字符串，仅取前两个字符；若为空则清屏
     * @param keep_alive true：阻塞直到 Ctrl+C，否则立即返回
     */
    void displayStaticChars(const char* text, bool keep_alive = true);    
    
    /**
     * @brief 清除 LED 矩阵（全灭）
     */
    void clear();

    /**
     * @brief 全亮测试
     */
    void fill();

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace led_matrix

#endif // LED_MATRIX_HPP