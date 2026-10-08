# syj_led_matrix

基于 **CH347（USB→SPI 桥接）** + **MAX7219** 的 8x8 LED 点阵屏 Linux 用户态驱动。

支持静态双字符显示、滚动文本、清屏、全亮测试四种模式。

---

## 仓库结构

```
syj_led_matrix/
├── zifu-main/                # 字模（font5x7）+ 原始 CH347 调用示例（上游交付包）
├── led_matrix_driver_src/    # LedMatrix C++ 类库的源代码
└── led_matrix_driver_sdk/    # 编译好的 SDK（.so + 头文件 + 示例，给应用开发者用）
```

### 三个目录的关系

```
zifu-main                       led_matrix_driver_src                led_matrix_driver_sdk
(字模 + ch347 demo)     ───►    (封装成 C++ 类库)          ───►        (编译好的 .so + 示例)
                                                                       │
                                                                       ▼
                                                              你的应用 #include "led_matrix.hpp"
```

| 目录 | 关键文件 | 什么时候用 |
|---|---|---|
| `zifu-main/` | `font5x7.c/h`（字模）、`font5x7_demo.c`（直接调 libch347 的 demo）、`ch347_lib.h`、`libch347.so`、接口文档 docx | 想看 CH347 原始 API、想看字模如何映射到 MAX7219、想脱离 SDK 自己写底层 |
| `led_matrix_driver_src/` | `src/led_matrix.cpp`（驱动实现）、`include/led_matrix.hpp`（对外接口）、`examples/demo.cpp` | 想改驱动行为（比如改 SPI 速率、改默认亮度、加新效果）、想重新出 .so |
| `led_matrix_driver_sdk/` | `lib/libled_matrix.so*`、`lib/libch347.so*`、`include/led_matrix.hpp`、`example/main.cpp` | **绝大多数场景**。直接链接到你的应用 |

> **一句话**：用 SDK；要改驱动就看 src；要看 ch347 原始调用就翻 zifu-main。

---

## 适用硬件

### 必需的硬件

| 部件 | 型号 | 作用 |
|---|---|---|
| USB 桥接芯片 | **CH347**（沁恒 WCH，VID:PID = `1A86:55DC`） | 把 USB 转成 SPI |
| LED 驱动芯片 | **MAX7219** | 驱动 8x8 点阵 |
| 点阵屏 | 8x8 共阴 LED 点阵（**支持 2 块级联**，SDK 默认按双块布局） | 显示 |
| 主控 | 任何带 USB 的 Linux 板子（x86_64 / ARM64） | 运行程序 |

### 已验证的主控

- NVIDIA Jetson Thor（ARM64）
- x86_64 Linux PC

其他 Linux 板子理论可行，只需：
1. 有 USB 口
2. 内核启用 hidraw（现代发行版默认启用）

### 接线

```
Linux 主机            CH347 模块           MAX7219          8x8 点阵
  USB  ──────►  USB
                  SPI_MOSI  ──────►  DIN
                  SPI_CLK   ──────►  CLK
                  SPI_CS    ──────►  CS/LOAD
                  5V        ──────►  VCC
                  GND       ──────►  GND                ──►  驱动 LED
```

**级联**：第一块的 DOUT 接第二块的 DIN，CLK/CS/VCC/GND 并联。

### CH347 工作模式说明

CH347 是多功能芯片，本仓库用的是 **HID 模式 + SPI 接口**：
- 插入后会枚举出**两个** hidraw 节点（如 `/dev/hidraw0` 和 `/dev/hidraw1`）
- interface 0 = UART（不用）
- **interface 1 = SPI/I2C（本仓库用这个）**

---

## udev 规则（重要）

### 为什么需要

CH347 插入后内核分配的 `/dev/hidrawN` 编号是**动态的**：
- 今天插第一个口是 `hidraw1`，明天换个口就可能是 `hidraw4`
- 应用程序写死 `/dev/hidraw1` 不可靠

udev 规则的作用：**给 CH347 的 SPI 接口创建一个固定软链接 `/dev/led_spi`**，应用程序统一访问这个固定路径。

### 规则内容

`udev/99-led-matrix.rules`：

```
ACTION=="add|change", SUBSYSTEM=="hidraw", KERNELS=="0003:1A86:55DC.*", ENV{DEVPATH}=="*:1.1/*", SYMLINK+="led_spi", MODE="0666"
```

字段逐一解释：

| 字段 | 含义 |
|---|---|
| `ACTION=="add\|change"` | 设备插入或属性变化时触发 |
| `SUBSYSTEM=="hidraw"` | 只匹配 hidraw 子系统 |
| `KERNELS=="0003:1A86:55DC.*"` | 匹配 USB VID:PID = `1A86:55DC`（CH347 的 HID 模式 ID） |
| `ENV{DEVPATH}=="*:1.1/*"` | 只匹配 interface 1（SPI/I2C），不匹配 interface 0（UART） |
| `SYMLINK+="led_spi"` | 创建 `/dev/led_spi` 软链接到实际的 `/dev/hidrawN` |
| `MODE="0666"` | 所有用户可读写（不用 sudo 也能跑） |

### 安装

```bash
# 1. 复制规则
sudo cp led_matrix_driver_sdk/udev/99-led-matrix.rules /etc/udev/rules.d/

# 2. 重新加载
sudo udevadm control --reload-rules

# 3. 触发一次（已插着 CH347 时）
sudo udevadm trigger --subsystem-match=hidraw

# 4. 验证
ls -l /dev/led_spi
# 期望：lrwxrwxrwx ... /dev/led_spi -> hidraw1
```

或者**重新插拔** CH347，规则自动生效。

### 排查

```bash
# 看 CH347 是否被 USB 子系统识别（应看到 1a86:55dc）
lsusb | grep -i 1a86

# 看 hidraw 节点
ls -la /dev/hidraw*

# 看每个节点的 interface（找 DEVPATH 含 ":1.1" 的）
udevadm info -a /dev/hidraw0 | grep -E "DEVPATH|INTERFACE"

# 测试规则是否会命中
udevadm test /dev/hidraw1 2>&1 | grep led_spi
```

如果你的 CH347 VID:PID 不是 `1a86:55DC`（不同固件版本可能不同），用 `lsusb` 看实际值，改规则文件里 `KERNELS=="0003:VID:PID.*"` 那一段。

---

## 快速上手（用 SDK）

```bash
# 1. 装好 udev 规则（见上一节）

# 2. 编译示例
cd led_matrix_driver_sdk
mkdir -p build && cd build
cmake ..
make

# 3. 跑（默认静态显示 "AB"）
./led_demo
```

### 示例程序的 4 种模式

| 命令 | 效果 |
|---|---|
| `./led_demo` 或 `./led_demo static` | 左屏 `A` 右屏 `B`，阻塞到 Ctrl+C |
| `./led_demo scroll` | `ABCDEFG` 从右向左滚动 |
| `./led_demo clear` | 清屏（全灭） |
| `./led_demo fill` | 全亮测试 |
| `./led_demo /dev/hidraw1 static` | 指定设备节点（参数顺序可换） |

---

## 在自己代码里调用 SDK

### API 一览（`include/led_matrix.hpp`）

```cpp
#include "led_matrix.hpp"

using namespace led_matrix;

// 构造：自动打开设备并初始化（默认 /dev/led_spi，可传 /dev/hidrawN）
LedMatrix led;                            // 或 LedMatrix led("/dev/hidraw1");

// 静态显示最多 2 个字符（左右两块各一个）
led.displayStaticChars("AB");             // keep_alive=true（默认），阻塞
led.displayStaticChars("OK", false);      // 立即返回

// 滚动显示文本（阻塞）
led.scrollText("HELLO");
led.scrollText("HELLO", 50000);                       // 50ms 帧间隔
led.scrollText("HELLO", 80000, ScrollDir::RIGHT);     // 改方向

// 清屏
led.clear();

// 全亮测试
led.fill();
```

### 编译时链接

```bash
g++ my_app.cpp -o my_app \
    -I led_matrix_driver_sdk/include \
    -L led_matrix_driver_sdk/lib \
    -lled_matrix -lch347 \
    -Wl,-rpath,$(pwd)/led_matrix_driver_sdk/lib
```

`-Wl,-rpath` 让程序运行时能找到 `.so`（避免每次都要 `export LD_LIBRARY_PATH`）。

完整可参考 `led_matrix_driver_sdk/example/main.cpp`。

---

## 改驱动？看 src

`led_matrix_driver_src/` 是 SDK 的源代码。

### 目录

```
led_matrix_driver_src/
├── include/led_matrix.hpp     # 对外接口
├── src/
│   ├── led_matrix.cpp         # 驱动实现（PIMPL 模式）
│   ├── font5x7.c              # 字模数据
│   └── font5x7.h
├── lib/
│   ├── ch347_lib.h            # CH347 头文件
│   └── libch347.so*           # CH347 库
├── examples/demo.cpp          # 调用示例
└── udev/99-led-matrix.rules
```

### 可调参数（`led_matrix.cpp` 顶部常量）

```cpp
static constexpr int SPI_HZ          = 1000000;   // SPI 速率
static constexpr uint8_t INTENSITY   = 8;         // 亮度 0-15
static constexpr bool FIX_SWAP       = false;     // 左右屏互换
static constexpr bool FIX_FLIPX      = true;      // 水平翻转
static constexpr bool FIX_FLIPY      = false;     // 垂直翻转
static constexpr bool FIX_TRANSPOSE  = false;     // 转置
```

改完重新编译：

```bash
cd led_matrix_driver_src
mkdir -p build && cd build
cmake ..
make
# 生成 libled_matrix.so.1.0.0 + demo
```

---

## zifu-main（最原始的字模 demo）

这是上游交付的"字模 + ch347 调用示例"包，**不依赖 SDK**，适合：

- 想看 CH347 原始 API（`ch347_lib.h` 里的函数）
- 想看字模如何映射到 MAX7219 寄存器
- 想完全脱离 SDK 写底层

### 内容

```
zifu-main/
├── font5x7.c / font5x7.h            # 5x7 ASCII 字模
├── font5x7_demo.c                   # 演示程序（终端打印 + 硬件滚动两种模式）
├── ch347_lib.h                      # CH347 API 头文件
├── libch347.so / libch347.so.1      # CH347 用户态库
└── font5x7_点阵字符库___接口交付文档.docx   # 上游接口文档
```

### 编译运行

```bash
cd zifu-main

# 1. 把 libch347.so 放到系统库目录（一次性）
sudo cp libch347.so* /usr/lib/
sudo ldconfig

# 2. 编译
gcc -O2 -Wall font5x7_demo.c font5x7.c -o font5x7_demo -lch347

# 3. 终端模式（不需要硬件，先在终端 ASCII 打印预览字模）
./font5x7_demo
./font5x7_demo --text "Hello"

# 4. 硬件模式
sudo ./font5x7_demo /dev/hidraw1
sudo ./font5x7_demo /dev/hidraw1 --text "Hi!" --hz 1000000 --intensity 8
```

---

## 常见问题

### Q1: 插上 CH347 后没有 `/dev/hidraw*`？

```bash
lsusb | grep -i 1a86       # 看 USB 层是否识别
dmesg | tail -20           # 看内核日志
```

`lsusb` 都看不到 → 硬件连接问题（换 USB 口、换线、检查供电）。

### Q2: 有 hidraw 但 `/dev/led_spi` 没出现？

udev 规则没生效。回到上面"udev 规则"小节，用 `udevadm test` 排查。

### Q3: 程序报 "Permission denied" 打开 `/dev/led_spi`？

```bash
# 临时：sudo 跑
sudo ./led_demo

# 长期：确认 udev 规则 MODE="0666" 已生效
ls -l /dev/led_spi
# 期望：crw-rw-rw-
```

### Q4: 显示乱码 / 不亮 / 顺序反了？

- 检查 MAX7219 接线（DIN/CLK/CS/VCC/GND）
- 供电是否 5V（不要用 3.3V）
- 多块级联时 DOUT→DIN 是否连对
- 字符方向反了 → 改 `led_matrix.cpp` 顶部的 `FIX_FLIPX / FIX_FLIPY / FIX_SWAP / FIX_TRANSPOSE` 常量，重新编译

### Q5: 怎么找当前 hidraw 编号对应的设备？

```bash
for f in /dev/hidraw*; do
  echo "=== $f ==="
  udevadm info -a "$f" | grep -E "idVendor|idProduct|INTERFACE" | head -5
done
```

### Q6: 字模库里支持哪些字符？

看 `led_matrix_driver_src/src/font5x7.c`。5x7 字模一般覆盖可打印 ASCII（`0x20` 空格 到 `0x7E` `~`）。`scrollText` / `displayStaticChars` 只能显示字模里有的字符。

---

## 依赖与致谢

- **CH347 用户态库** `libch347.so`：来自[沁恒官网](https://www.wch.cn/products/CH347.html)
- **5x7 字模**：经典 5x7 ASCII 字模数据
- 其余代码由本仓库作者编写

---

## 目录速查

| 我想... | 去哪个目录 |
|---|---|
| 直接用 LED 屏做应用 | `led_matrix_driver_sdk/` |
| 改驱动行为、加新效果 | `led_matrix_driver_src/` |
| 学习字模原理 / CH347 原始 API | `zifu-main/` |
| 配 udev 规则 | `led_matrix_driver_sdk/udev/`（与 `src/udev/` 同一份） |
