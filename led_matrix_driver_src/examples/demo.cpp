/**
 * demo.cpp —— libled_matrix 示例程序
 *
 * 命令行用法：
 *   ./demo                              # 默认打开 /dev/led_spi，static 模式
 *   ./demo static                       # /dev/led_spi，静态显示 AB
 *   ./demo /dev/hidraw4                 # 指定设备，static 模式
 *   ./demo /dev/hidraw4 fill            # 指定设备，全亮测试
 *   ./demo scroll /dev/hidraw4          # 模式与路径顺序可互换
 *   ./demo clear                        # 清屏
 *
 * 如果程序无法访问设备，可尝试加上 sudo。
 */

#include "led_matrix.hpp"
#include <iostream>
#include <string>

// 打印使用说明
static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog
              << " [/dev/led_spi|/dev/hidrawN] [static|scroll|clear|fill]"
              << std::endl;
    std::cout << "   or: " << prog
              << " [static|scroll|clear|fill] [/dev/led_spi|/dev/hidrawN]"
              << std::endl;
    std::cout << "  static : 静态显示 AB   (默认)" << std::endl;
    std::cout << "  scroll : 滚动显示 ABCDEFG" << std::endl;
    std::cout << "  clear  : 清屏" << std::endl;
    std::cout << "  fill   : 全亮测试" << std::endl;
    std::cout << "按 Ctrl+C 可停止 static/scroll 模式。" << std::endl;
}

// 判断参数字符串是否是设备节点路径（以 /dev/ 开头）
static bool looksLikeDevicePath(const char* s) {
    return s && (std::string(s).find("/dev/") == 0);
}

int main(int argc, char* argv[]) {
    // 默认值：使用 udev 规则创建的软链接 /dev/led_spi，模式为 static
    std::string mode = "static";
    std::string device_path = "/dev/led_spi";

    // 解析命令行参数：/dev/... 识别为设备节点，其余识别为模式
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "help" || arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (looksLikeDevicePath(arg.c_str())) {
            device_path = arg;
        } else {
            mode = arg;
        }
    }

    try {
        // 打开设备并初始化 SPI + MAX7219
        led_matrix::LedMatrix led(device_path.c_str());

        if (mode == "scroll") {
            std::cout << "滚动显示 ABCDEFG... (Ctrl+C 停止)" << std::endl;
            led.scrollText("ABCDEFG");
        } else if (mode == "clear") {
            std::cout << "清屏" << std::endl;
            led.clear();
        } else if (mode == "fill") {
            std::cout << "全亮测试... (Ctrl+C 停止)" << std::endl;
            led.fill();
        } else if (mode == "static") {
            std::cout << "静态显示 AB... (Ctrl+C 停止)" << std::endl;
            led.displayStaticChars("AB");
        } else {
            std::cerr << "未知模式: " << mode << std::endl;
            printUsage(argv[0]);
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
