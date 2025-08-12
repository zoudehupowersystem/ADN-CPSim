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

// 全局调度器指针的定义和初始化。
// 此指针将在程序开始时被赋值为创建的调度器实例。
// 其他模块可以通过 extern 声明来访问此全局调度器
extern cps_coro::Scheduler* g_scheduler; //= nullptr;

// 函数：获取当前进程的峰值内存使用量 (KB)
// 这是一个平台相关的函数，尝试获取程序运行到目前为止所达到的最大物理内存占用。
extern long get_peak_memory_usage_kb();

extern void avc_test_non_realtime();
extern void avc_test_realtime();

extern void test_vpp();
int main() // 虚拟电厂频率响应仿真
{
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