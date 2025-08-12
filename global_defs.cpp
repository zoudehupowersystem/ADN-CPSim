#include "cps_coro_lib.h"
#include "logging_utils.h"

#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

cps_coro::Scheduler* g_scheduler = nullptr;

long get_peak_memory_usage_kb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return static_cast<long>(pmc.PeakWorkingSetSize / 1024);
    }
    if (g_console_logger)
        g_console_logger->error("在Windows上获取峰值内存使用信息失败，错误码: {}", GetLastError());
    return -1;
#elif defined(__linux__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss;
    }
    if (g_console_logger)
        g_console_logger->error("在Linux上通过getrusage获取峰值内存使用信息失败。");
    return -1;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t taskInfo;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&taskInfo, &infoCount) == KERN_SUCCESS) {
        return static_cast<long>(taskInfo.resident_size_max / 1024);
    }
    if (g_console_logger)
        g_console_logger->error("在macOS上通过task_info获取峰值内存使用信息失败。");
    return -1;
#else
    if (g_console_logger)
        g_console_logger->warn("此函数当前不支持在本平台上获取峰值内存使用统计数据。");
    return -1;
#endif
}