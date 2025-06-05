// simulation_events_and_data.h
// 定义了仿真中使用的全局事件ID和核心数据结构。
// 这些定义在项目的多个模块之间共享，用于实现模块间的通信和数据交换。
#ifndef SIMULATION_EVENTS_AND_DATA_H
#define SIMULATION_EVENTS_AND_DATA_H

#include "cps_coro_lib.h" // 引入协程库以使用 cps_coro::EventId 类型
#include "ecs_core.h" // 引入ECS核心库以使用 Entity 类型

// --- 通用仿真事件ID ---
// 这些事件ID用于跨不同仿真模块的通用交互和信令。
// 使用 constexpr 确保这些ID是编译期常量。

constexpr cps_coro::EventId GENERATOR_READY_EVENT = 1; // 发电机已准备就绪并成功并网事件
constexpr cps_coro::EventId LOAD_CHANGE_EVENT = 2; // 负荷发生变化事件 (通用，可携带具体变化数据)

constexpr cps_coro::EventId BREAKER_OPENED_EVENT = 6; // 断路器成功断开事件 (通常由保护动作或手动操作触发)
constexpr cps_coro::EventId STABILITY_CONCERN_EVENT = 7; // 系统出现稳定性风险或告警事件
constexpr cps_coro::EventId LOAD_SHED_REQUEST_EVENT = 8; // 请求执行切负荷操作的事件
constexpr cps_coro::EventId POWER_ADJUST_REQUEST_EVENT = 9; // 请求调整发电机或其他资源功率输出的事件

// --- 保护系统专用事件ID（这是非常简化的保护仿真） ---
// 这些事件ID专用于继电保护仿真模块内部或与保护系统紧密相关的交互。
constexpr cps_coro::EventId FAULT_INFO_EVENT_PROT = 100; // 故障信息事件 (用于向保护系统通报新发生的故障详情)
constexpr cps_coro::EventId ENTITY_TRIP_EVENT_PROT = 101; // 设备跳闸指令事件 (由保护系统发出，指令特定设备跳闸)

// --- 逻辑保护仿真专用事件ID （这是相对复杂的保护仿真）---
// 这些ID用于在逻辑保护仿真模块内部或相关模块间传递特定事件。
constexpr cps_coro::EventId LOGIC_FAULT_EVENT = 300; // 逻辑故障发生事件
constexpr cps_coro::EventId LOGIC_BREAKER_TRIP_COMMAND_EVENT = 301; // 逻辑断路器跳闸命令事件
constexpr cps_coro::EventId LOGIC_BREAKER_STATUS_CHANGED_EVENT = 302; // 逻辑断路器状态变更事件 (例如: 打开/关闭)

// --- 频率-有功响应系统专用事件ID ---
// 这些事件ID专用于频率和有功功率响应仿真模块。
constexpr cps_coro::EventId FREQUENCY_UPDATE_EVENT = 200; // 系统频率更新事件 (通报当前系统频率或频率偏差)

// --- 事件ID定义 (AVC仿真场景专用) ---
// 为避免与项目中其他部分的事件ID潜在冲突，并保持此仿真示例的模块化，
// 可以定义专用于此AVC场景的事件ID。
// 假设这里使用专用的ID：
constexpr cps_coro::EventId VOLTAGE_CHANGE_EVENT_AVC = 10000; // 电压变化事件ID (AVC场景)
constexpr cps_coro::EventId LOAD_CHANGE_EVENT_AVC = 10001; // 负荷变化事件ID (AVC场景)

// --- 核心数据结构 ---

// 故障信息结构体 (Fault Information Structure)
// 封装了描述电力系统故障的关键参数。
struct FaultInfo {
    double current_kA = 0.0; // 故障电流大小 (单位: 千安培 kA)
    double voltage_kV = 220.0; // 故障点电压 (单位: 千伏 kV)。可以是故障前的额定电压，或故障发生时的实际残压。
    double impedance_Ohm = 0.0; // 故障回路的总阻抗，或测量点到故障点的阻抗 (单位: 欧姆 Ohm)
    double distance_km = 0.0; // 故障点距离测量点的物理距离 (单位: 千米 km)，主要用于线路距离保护。
    Entity faulty_entity_id = 0; // 发生故障的设备 (如线路、变压器) 的实体ID。0可能表示未知或不适用。

    // 辅助函数：如果阻抗未直接提供，但电压和电流数据有效，则尝试计算故障阻抗。
    // 注意：这是一个非常简化的计算 (Z = V/I)，实际电力系统故障分析中的阻抗计算要复杂得多，
    //       需要考虑相角、系统参数等。此处仅为示例。
    void calculate_impedance_if_needed()
    {
        if (impedance_Ohm == 0.0 && voltage_kV > 0 && current_kA > 0) {
            // 阻抗 Z = V / I。确保单位一致：电压(V) / 电流(A)。
            impedance_Ohm = (voltage_kV * 1000.0) / (current_kA * 1000.0);
        }
    }
};

// 频率信息结构体 (Frequency Information Structure)
// 封装了系统频率相关的信息，用于频率响应仿真。
struct FrequencyInfo {
    double current_sim_time_seconds; // 事件发生时的当前仿真时间 (单位: 秒)
    double freq_deviation_hz; // 系统频率相对于标称频率 (例如中国50Hz, 美国60Hz) 的偏差值 (单位: 赫兹 Hz)。
                              // 负值表示频率降低 (欠频)，正值表示频率升高 (过频)。
};

// --- 辅助日志函数---
// 该函数用于在控制台和日志文件中输出带有仿真时间戳的格式化日志信息。
template <typename... Args>
inline void log_lp_info(cps_coro::Scheduler& scheduler, const char* user_format_str, Args&&... args)
{
    if (g_console_logger) {
        char time_prefix_buf[64];
        snprintf(time_prefix_buf, sizeof(time_prefix_buf), "[LP-Sim @ %lldms] ", (long long)scheduler.now().time_since_epoch().count());

        char user_message_buf[1024]; // 假设用户消息部分不会超过这个长度
        snprintf(user_message_buf, sizeof(user_message_buf), user_format_str, std::forward<Args>(args)...);

        std::string final_message = time_prefix_buf;
        final_message += user_message_buf;
        g_console_logger->info(final_message);
    } else {
        std::cout << "[LP-Sim @ " << scheduler.now().time_since_epoch().count() << "ms] ";
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), user_format_str, std::forward<Args>(args)...);
        std::cout << buffer << std::endl;
    }
}

#endif // SIMULATION_EVENTS_AND_DATA_H