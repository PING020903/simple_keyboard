cmake_minimum_required(VERSION 3.10)

project(simple_keyboard
    VERSION 0.4.1
    DESCRIPTION "A lightweight embedded key abstraction library with debounce, hold timing and per-key handler callbacks"
    LANGUAGES C)

# 指定C编译器路径
#set (CMAKE_C_COMPILER "F:/llvm-mingw-20220906-msvcrt-i686/bin/i686-w64-mingw32-gcc")

# 指定C++编译器路径
#set (CMAKE_CXX_COMPILER "F:/llvm-mingw-20220906-msvcrt-i686/bin/i686-w64-mingw32-g++")

# 配置指定的编译器
#set within user supplied toolchain file
#set(CMAKE_C_COMPILER clang)

#[[
# 指定C编译器路径
set(CMAKE_C_COMPILER "F:/llvm-mingw-20220906-msvcrt-i686/bin/gcc.exe" CACHE FILEPATH "C compiler")

# 指定C++编译器路径
set(CMAKE_CXX_COMPILER "F:/llvm-mingw-20220906-msvcrt-i686/bin/g++.exe" CACHE FILEPATH "C++ compiler")
#]]

# 设置 C99 标准（嵌入式主流标准，.clangd 同步为 -std=gnu99）
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # 使用 gnu99（与 .clangd / 门禁工程一致）

# 设置头文件搜索路径
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/thirdparty/ringBuffer/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/thirdparty/EventScheduling/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/thirdparty/c-linked-list/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/thirdparty/debounce/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/thirdparty/memguard/include)

# 遍历当前目录下的所有源文件, 并获取到 SOURCES
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/sources/*.c)
file(GLOB_RECURSE THIRDPARTY_SOURCES ${PROJECT_SOURCE_DIR}/thirdparty/**/*.c)

# 设置 "./build/bin" 目录作为可执行程序输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

# 调试输出开关（栈/代码开销敏感时建议全部关闭）：
#  SKB_DEBUG         : simple_keyboard 组件内部调试打印，默认 OFF
#  SKB_DEMO_PRINTS   : 示例程序的 DEBUG_PRINT 等打印宏（DBG_ENABLE），默认 ON
#  SKB_ARG_CHECK     : API 参数校验（SKB_ARG_CHECK_ENABLE，含指针 NULL 与配置合法性），默认 ON；
#                      关闭后视为调用者完全可信，非法入参将导致未定义行为
option(SKB_DEBUG "Enable simple_keyboard internal debug prints" OFF)
option(SKB_DEMO_PRINTS "Enable demo debug prints (DBG_ENABLE)" ON)
option(SKB_ARG_CHECK "Enable API argument validation (SKB_ARG_CHECK_ENABLE)" ON)

# 键盘特性开关（编译期选择性开启，省 Flash/RAM/每 tick CPU）
option(SKB_CFG_ENABLE_PRESSED "Enable press-edge event (on_press)" ON)
option(SKB_CFG_ENABLE_CLICK "Enable click event (on_click)" ON)
option(SKB_CFG_ENABLE_LONG "Enable long-press events (on_long_start/on_long_up)" ON)
option(SKB_CFG_ENABLE_REPEAT "Enable long-press auto repeat (on_long_hold)" ON)

add_executable(outputFile ${SOURCES} ${THIRDPARTY_SOURCES})

# Windows 多媒体定时器（timeBeginPeriod）：提高 Sleep 精度到 1ms
target_link_libraries(outputFile winmm)

# 调度器内部每 ms 入队/出队调试打印会刷屏（EVT_TICK 每 1ms 一次），
# 仅将该文件编译为 DBG_ENABLE=0，其余文件保持调试开启。
set_source_files_properties(${PROJECT_SOURCE_DIR}/thirdparty/EventScheduling/sources/EventSchedul.c
    PROPERTIES COMPILE_DEFINITIONS "DBG_ENABLE=0")

if(SKB_DEBUG)
    target_compile_definitions(outputFile PRIVATE SKB_DEBUG)
endif()
if(NOT SKB_DEMO_PRINTS)
    target_compile_definitions(outputFile PRIVATE DBG_ENABLE=0)
endif()
if(NOT SKB_ARG_CHECK)
    target_compile_definitions(outputFile PRIVATE SKB_ARG_CHECK_ENABLE=0)
endif()
if(NOT SKB_CFG_ENABLE_PRESSED)
    target_compile_definitions(outputFile PRIVATE SKB_CFG_ENABLE_PRESSED=0)
endif()
if(NOT SKB_CFG_ENABLE_CLICK)
    target_compile_definitions(outputFile PRIVATE SKB_CFG_ENABLE_CLICK=0)
endif()
if(NOT SKB_CFG_ENABLE_LONG)
    target_compile_definitions(outputFile PRIVATE SKB_CFG_ENABLE_LONG=0)
endif()
if(NOT SKB_CFG_ENABLE_REPEAT)
    target_compile_definitions(outputFile PRIVATE SKB_CFG_ENABLE_REPEAT=0)
endif()
