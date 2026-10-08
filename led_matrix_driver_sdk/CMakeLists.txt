cmake_minimum_required(VERSION 3.10)
project(led_matrix_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)

# 头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 优先在项目本地 lib 目录中查找库，找不到时允许回退到系统默认路径
find_library(LED_MATRIX_LIB NAMES led_matrix
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib
    REQUIRED)

find_library(CH347_LIB NAMES ch347
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib
    REQUIRED)

# 关键：让生成的可执行文件运行时自动去 SDK 的 lib 目录找库
# 使用 $ORIGIN 相对路径，便于 SDK 目录整体移动后仍能运行
set(CMAKE_BUILD_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

add_executable(led_demo example/main.cpp)
target_link_libraries(led_demo ${LED_MATRIX_LIB} ${CH347_LIB} pthread)
