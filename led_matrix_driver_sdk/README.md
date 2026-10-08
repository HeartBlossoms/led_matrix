# LED Matrix Driver SDK

本 SDK 提供对 LED 点阵屏（基于 MAX7219 + CH347）的简易应用层接口，支持静态双字符显示和滚动文本显示。

---

## 目录结构

```text
led_matrix_driver_sdk/
├── CMakeLists.txt          # SDK 编译脚本
├── README.md               # 本文档
├── example/
│   └── main.cpp            # 示例程序
├── include/
│   └── led_matrix.hpp      # 应用层头文件
└── lib/
    ├── libch347.so*        # CH347 USB/SPI 驱动库
    └── libled_matrix.so*   # LED 矩阵核心库
```

---

`sdk/lib` 目录已包含运行所需动态库：

| 库 | 说明 |
|---|---|
| `libch347.so` | CH347 USB/SPI 硬件驱动 |
| `libled_matrix.so` | LED 矩阵控制库 |

---

## 编译

```bash
cd led_matrix_driver_sdk/build
cmake ..
make
```

编译成功后生成可执行文件：

```text
build/led_demo
```

---

## 运行

示例程序支持 4 种运行模式，并可手动指定设备节点（无法运行时，加上 `sudo` 权限）：

```bash
./led_demo                          # 默认 /dev/led_spi，static 模式
./led_demo static                   # 同上
./led_demo /dev/hidraw4 static      # 指定设备节点
./led_demo static /dev/hidraw4      # 模式与路径顺序可互换
```

### 1. 静态显示 `AB`（默认）

```bash
./led_demo
# 或
./led_demo static
```

左右两块 8x8 点阵分别显示 `A` 和 `B`，程序保持阻塞，按 `Ctrl+C` 退出。

### 2. 滚动显示 `ABCDEFG`

```bash
./led_demo scroll
```

字符串 `ABCDEFG` 从右向左滚动显示，循环滚动，按 `Ctrl+C` 退出。

### 3. 清屏

```bash
./led_demo clear
```

熄灭所有 LED，执行后立即返回。

### 4. 全亮测试

```bash
./led_demo fill
```

点亮所有 LED，按 `Ctrl+C` 退出。


---

## 设备节点

不同机器上 CH347 对应的 `/dev/hidrawN` 编号可能不同（例如你本地是 `hidraw1`，上游是 `hidraw4`）。
为了避免写死编号，SDK 默认使用 udev 规则创建的固定软链接：

```text
/dev/led_spi
```

### 安装 udev 规则

把 `udev/99-led-matrix.rules` 复制到目标系统的 udev 规则目录，然后重新加载：

```bash
sudo cp led_matrix_driver_sdk/udev/99-led-matrix.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

或者重新插拔 CH347。加载成功后会出现 `/dev/led_spi`：

```bash
ls -l /dev/led_spi
```

该规则会把 CH347 的 SPI/I2C HID 接口（通常是 `bInterfaceNumber==01`）映射为 `/dev/led_spi`，并设置 `MODE="0666"` 方便程序访问。

### 手动指定设备节点

如果目标系统没有安装 udev 规则，或者你想临时指定其他节点，仍然可以在构造时传入路径：

```cpp
led_matrix::LedMatrix led("/dev/hidraw4");
```

这样不会依赖 `/dev/led_spi`。


## 应用层 API

头文件：`include/led_matrix.hpp`

```cpp
namespace led_matrix {

enum class ScrollDir { LEFT, RIGHT };

class LedMatrix {
public:
    // 打开设备并初始化硬件
    explicit LedMatrix(const char* device_path = "/dev/led_spi");

    // 滚动显示文本（阻塞，Ctrl+C 退出）
    void scrollText(const char* text,
                    int frame_us = 80000,
                    ScrollDir dir = ScrollDir::LEFT);

    // 静态显示最多两个字符
    // keep_alive=true 时阻塞直到 Ctrl+C
    void displayStaticChars(const char* text, bool keep_alive = true);

    // 清屏
    void clear();

    // 全亮测试
    void fill();
};

} // namespace led_matrix
```

---

## 常见问题

### 1. 提示权限不足 `/dev/hidraw0`

运行时加上 `sudo` 权限：

```bash
sudo ./led_demo
```

### 2. 符号链接失效

本 SDK 中的动态库使用符号链接管理版本：

```text
libch347.so -> libch347.so.1
libled_matrix.so -> libled_matrix.so.1 -> libled_matrix.so.1.0.0
```

将 SDK 上传 Git 后再拉取到本地时，符号链接可能会失效或变成长文本文件。此时请进入 `sdk/lib` 目录，按以下命令重新创建符号链接：

```bash
cd led_matrix_driver_sdk/lib

ln -sf libch347.so.1 libch347.so
ln -sf libled_matrix.so.1.0.0 libled_matrix.so.1
ln -sf libled_matrix.so.1 libled_matrix.so
```

创建后可用 `ls -l` 检查链接是否恢复正常：

```bash
ls -l
```

### 3. 找不到 `libch347.so` 或 `libled_matrix.so`

请检查 `sdk/lib` 目录下是否存在以下文件：

```text
libch347.so -> libch347.so.1
libch347.so.1
libled_matrix.so -> libled_matrix.so.1 -> libled_matrix.so.1.0.0
libled_matrix.so.1.0.0
```

如果包含 `.so` 文件，但仍然无法编译，那就把 `/lib/` 目录下 `libch347.so`、`libled_matrix.so.1.0.0` 给放到 `/usr/lib` 目录下。

