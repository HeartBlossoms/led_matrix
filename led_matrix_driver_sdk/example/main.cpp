#include "led_matrix.hpp"
#include <iostream>
#include <string>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [/dev/led_spi|/dev/hidrawN] [static|scroll|clear|fill]" << std::endl;
    std::cout << "   or: " << prog << " [static|scroll|clear|fill] [/dev/led_spi|/dev/hidrawN]" << std::endl;
    std::cout << "  static : 静态显示 AB   (默认)" << std::endl;
    std::cout << "  scroll : 滚动显示 ABCDEFG" << std::endl;
    std::cout << "  clear  : 清屏" << std::endl;
    std::cout << "  fill   : 全亮测试" << std::endl;
    std::cout << "按 Ctrl+C 可停止 static/scroll 模式。" << std::endl;
}

static bool looksLikeDevicePath(const char* s) {
    return s && (std::string(s).find("/dev/") == 0);
}

int main(int argc, char* argv[]) {
    std::string mode = "static";
    std::string device_path = "/dev/led_spi";

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
