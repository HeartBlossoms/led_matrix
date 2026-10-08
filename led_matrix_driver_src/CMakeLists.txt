cmake_minimum_required(VERSION 3.10)
project(led_matrix_driver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 内部头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib      # 让编译器找到 ch347_lib.h
)

# 查找 CH347 动态库，优先在项目本地 lib 目录中搜索，
# 找不到时允许回退到系统默认路径（如 /usr/lib）
find_library(CH347_LIB NAMES ch347
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib
    REQUIRED)

# 构建 libled_matrix.so
add_library(led_matrix SHARED
    src/led_matrix.cpp
    src/font5x7.c
)
set_target_properties(led_matrix PROPERTIES
    OUTPUT_NAME led_matrix
    VERSION "1.0.0"
    SOVERSION "1"
    # 构建时优先在 build 目录自身找，再回退到本地 lib；
    # 被复制到 sdk/lib 后，$ORIGIN 指向 sdk/lib，可独立运行
    BUILD_RPATH "$ORIGIN;${CMAKE_CURRENT_SOURCE_DIR}/lib"
    INSTALL_RPATH "$ORIGIN"
)
target_link_libraries(led_matrix PRIVATE ${CH347_LIB})

# 示例程序
add_executable(demo examples/demo.cpp)
target_link_libraries(demo led_matrix)
set_target_properties(demo PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/lib")