// main.cpp
// 主程序入口点
// 同时包含一些性能度量（如内存使用）的辅助函数

#include "cps_coro_lib.h" // 核心协程库
#include "ecs_core.h" // 实体组件系统核心
#include "frequency_system.h" // 频率响应仿真模块
#include "logging_utils.h" // 日志工具模块
#include "logic_protection_system.h" //保护逻辑仿真
#include "protection_system.h" // 继电保护仿真模块
#include "simulation_events_and_data.h" // 共享的事件ID和数据结构

#include <chrono> // C++标准时间库
#include <iomanip> // 用于输出格式化 (例如 std::fixed, std::setprecision)
#include <iostream> // 用于标准输入输出 (主要用于spdlog初始化失败时的回退)
#include <random> // 用于生成随机数 (例如初始化设备SOC)
#include <string> // C++标准字符串
#include <vector> // C++标准动态数组

// 用于获取内存使用情况的平台特定头文件
#if defined(_WIN32) // Windows平台
#include <psapi.h> // 需要链接 Psapi.lib
#include <windows.h>
#elif defined(__linux__) // Linux平台
#include <sys/resource.h> // for getrusage
#include <unistd.h> // for sysconf (如果需要获取页面大小等)
#elif defined(__APPLE__) // macOS平台
#include <mach/mach.h>
#endif

// 全局调度器指针的定义和初始化。
// 此指针将在程序开始时被赋值为创建的调度器实例。
// 其他模块可以通过 extern 声明来访问此全局调度器
cps_coro::Scheduler* g_scheduler = nullptr;

// 函数：获取当前进程的峰值内存使用量 (KB)
// 这是一个平台相关的函数，尝试获取程序运行到目前为止所达到的最大物理内存占用。
long get_peak_memory_usage_kb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    // 重要：必须先将结构体清零，然后设置cb成员为结构体的大小。
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return static_cast<long>(pmc.PeakWorkingSetSize / 1024); // PeakWorkingSetSize 单位是字节
    }
    // 获取失败
    if (g_console_logger)
        g_console_logger->error("在Windows上获取峰值内存使用信息失败，错误码: {}", GetLastError());
    return -1;
#elif defined(__linux__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss 在Linux上通常以KB为单位 (参见 man getrusage)
        return usage.ru_maxrss;
    }
    // 获取失败
    if (g_console_logger)
        g_console_logger->error("在Linux上通过getrusage获取峰值内存使用信息失败。");
    return -1;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t taskInfo;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&taskInfo, &infoCount) == KERN_SUCCESS) {
        // resident_size_max 在macOS上单位是字节
        return static_cast<long>(taskInfo.resident_size_max / 1024);
    }
    // 获取失败
    if (g_console_logger)
        g_console_logger->error("在macOS上通过task_info获取峰值内存使用信息失败。");
    return -1;
#else
    // 对于其他未支持的平台
    if (g_console_logger)
        g_console_logger->warn("此函数当前不支持在本平台上获取峰值内存使用统计数据。");
    return -1; // 返回-1表示无法获取
#endif
}

int main_protection() // 逻辑保护仿真
{
    std::cout << "--- 主动配电网CPS统一行为建模与高效仿真平台 ---\n";
    std::cout << "--- 场景: 保护与网络重构协同仿真 ---\n\n";

    // --- 创建调度器和ECS注册表实例 ---
    cps_coro::Scheduler scheduler;
    Registry registry;

    // --- 初始化仿真系统 ---
    LogicProtectionSystem protection_sim(registry, scheduler);
    protection_sim.initialize_scenario_entities();

    // --- 启动主仿真场景协程 ---
    protection_sim.simulate_fault_and_reconfiguration_scenario().detach();

    // --- 运行仿真 ---
    // 运行5秒仿真时间
    scheduler.run_until(scheduler.now() + std::chrono::seconds(20));

    std::cout << "\n--- 仿真循环结束 ---\n";

    return 0;
}

extern void avc_test_non_realtime();
extern void avc_test_realtime();

int main_avc() // AVC简化仿真
{
    initialize_loggers("AVC仿真.txt", true);

    std::cout << "========================================================================" << std::endl;
    std::cout << "信息: 即将运行自动电压控制 (AVC) 仿真示例..." << std::endl;
    std::cout << "========================================================================" << std::endl;
    avc_test_non_realtime(); // 运行AVC非实时仿真
    avc_test_realtime(); // 运行AVC实时仿真

    // 关闭日志系统，确保所有缓冲日志被刷新并释放资源
    shutdown_loggers();

    return 0;
}

extern void test_vpp();
int main() // 虚拟电厂频率响应仿真
{
    main_protection(); // 逻辑保护仿真
    return 0;

    // --- 初始化日志系统 ---
    // 日志文件名设为 "虚拟电厂频率响应数据.csv"，并在每次运行时覆盖旧文件 (truncate_data_log = true)。
    initialize_loggers("虚拟电厂频率响应数据.txt", true);

    std::cout << "========================================================================" << std::endl;
    std::cout << "信息: 即将运行虚拟电厂频率仿真示例..." << std::endl;
    std::cout << "========================================================================" << std::endl;

    test_vpp();

    // 关闭日志系统，确保所有缓冲日志被刷新并释放资源
    shutdown_loggers();
    return 0; // 程序正常退出
}