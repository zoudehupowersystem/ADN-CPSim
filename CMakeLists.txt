cmake_minimum_required(VERSION 3.20)
project(DualSimulationComparison LANGUAGES CXX)

# 设置 C++ 标准并要求
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # 推荐禁用GNU扩展以增强标准符合性

# --- 通用设置 ---
# spdlog 和 pthread 的查找
# 假设通过 apt install libspdlog-dev 安装了spdlog
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

# HECS + 协程版本的包含目录
# 假设 spdlog 由 find_package 处理，这里主要为项目内的头文件
# --- 逻辑保护仿真示例 ---
add_executable(logic_protection_demo
    logic_protection_main.cpp
    logic_protection_system.cpp
    PowerSystemTopology.cpp
    logging_utils.cpp
    global_defs.cpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(logic_protection_demo PRIVATE -fcoroutines -g -O3 -Wall)
else()
    message(WARNING "Logic protection target: Non-GCC compiler. Ensure C++20 and coroutine support.")
endif()

target_include_directories(logic_protection_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(logic_protection_demo PRIVATE
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(logic_protection_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- VPP 频率响应仿真示例 ---
add_executable(vpp_demo
    vpp_main.cpp
    vpp_system.cpp
    frequency_system.cpp
    logging_utils.cpp
    global_defs.cpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(vpp_demo PRIVATE -fcoroutines -g -O3 -Wall)
else()
    message(WARNING "VPP target: Non-GCC compiler. Ensure C++20 and coroutine support.")
endif()

target_include_directories(vpp_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vpp_demo PRIVATE
    spdlog::spdlog
    Threads::Threads
)

set_target_properties(vpp_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
# --- 传统的基于线程的对比仿真 ---
add_executable(traditional_threaded_simulation
    traditional_threaded_sim.cpp
)

# 传统线程版本的特定编译器标志 (不需要 -fcoroutines)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(traditional_threaded_simulation PRIVATE -O3 -Wall)
else()
    message(WARNING "Traditional threaded target: Non-GCC compiler.")
    # target_compile_options(traditional_threaded_simulation PRIVATE -O3 -Wall)
endif()

# 传统线程版本的链接库
target_link_libraries(traditional_threaded_simulation PRIVATE
    Threads::Threads # 它使用了 std::thread
)

set_target_properties(traditional_threaded_simulation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
