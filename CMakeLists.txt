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

add_executable(hecs_coro_simulation
    main.cpp
    avc_simulation.cpp
    frequency_system.cpp
    protection_system.cpp
    logic_protection_system.cpp
    logging_utils.cpp
    vpp_system.cpp
    PowerSystemTopology.cpp 
)

# HECS + 协程版本的特定编译器标志
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(hecs_coro_simulation PRIVATE -fcoroutines -g -O3 -Wall)
else()
    message(WARNING "HECS+Coroutine target: Non-GCC compiler. Ensure C++20 and coroutine support.")
    # 为其他编译器添加特定标志，例如 Clang 可能需要 -fcoroutines-ts
    # target_compile_options(hecs_coro_simulation PRIVATE -O3 -Wall) # Clang 的协程通常随-std=c++20启用
endif()

# HECS + 协程版本的包含目录
# 假设 spdlog 由 find_package 处理，这里主要为项目内的头文件
target_include_directories(hecs_coro_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # 用于本地头文件，如 logging_utils.h
    # 如果没有通过 find_package(spdlog) 而是本地包含spdlog头文件:
    # ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# HECS + 协程版本的链接库
target_link_libraries(hecs_coro_simulation PRIVATE
    spdlog::spdlog      # spdlog 提供的导入目标
    Threads::Threads    # pthread
)

set_target_properties(hecs_coro_simulation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- 目标 2: 传统的基于线程的对比仿真 ---
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

# 传统线程版本的包含目录 (如果它有本地头文件)
# target_include_directories(traditional_threaded_simulation PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )

# 传统线程版本的链接库
target_link_libraries(traditional_threaded_simulation PRIVATE
    Threads::Threads # 它使用了 std::thread
)

set_target_properties(traditional_threaded_simulation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- 构建信息 ---
message(STATUS "Build configured for ${CMAKE_CXX_COMPILER_ID} with C++${CMAKE_CXX_STANDARD}")
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
  message(STATUS "No CMAKE_BUILD_TYPE specified, defaulting to 'Debug'. Use -DCMAKE_BUILD_TYPE=Release for optimized builds.")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# 根据构建类型应用通用优化/调试标志 (如果需要，可以覆盖上面直接设置的 -O3)
# 例如，如果 CMAKE_BUILD_TYPE 为 Debug，则添加 -g
if (CMAKE_BUILD_TYPE MATCHES "^Debug$")
    target_compile_options(hecs_coro_simulation INTERFACE -g) # INTERFACE 会影响链接到它的目标
    target_compile_options(traditional_threaded_simulation INTERFACE -g)
    message(STATUS "Debug build: adding -g flag for both targets.")
elseif (CMAKE_BUILD_TYPE MATCHES "^Release$")
    # -O3 已在上面为GCC设置，这里可以确保 -DNDEBUG (禁用断言)
    target_compile_definitions(hecs_coro_simulation INTERFACE NDEBUG)
    target_compile_definitions(traditional_threaded_simulation INTERFACE NDEBUG)
    message(STATUS "Release build: NDEBUG defined for both targets. GCC uses -O3 as set above.")
endif()