// frequency_system.h
// 定义了与电力系统频率响应相关的组件、数据结构和协程任务。
// 这个模块通常用于模拟虚拟电厂 (VPP)、储能系统 (ESS) 或其他可调资源
// 如何响应系统频率偏差，以维持电网稳定。
#ifndef FREQUENCY_SYSTEM_H
#define FREQUENCY_SYSTEM_H

#include "cps_coro_lib.h" // 协程库，用于定义异步任务 (cps_coro::Task)
#include "ecs_core.h" // ECS核心库，用于定义和管理组件 (IComponent) 和实体 (Entity)
#include "simulation_events_and_data.h" // 包含仿真中共享的事件ID和数据结构定义
#include <cmath> // 标准数学函数库，例如 std::sin, std::cos, std::exp, std::abs
#include <string> // C++标准字符串类型
#include <vector> // C++标准动态数组 (向量)
// #include <fstream> // 不再需要 ofstream，因为日志记录已由 spdlog 或其他日志库处理

// 组件 (Component): 物理状态组件
// 存储参与频率调节的设备（如EV充电桩、储能单元）的当前关键物理状态。
struct PhysicalStateComponent : public IComponent {
    double current_power_kW; // 设备当前的实际功率 (kW)。
                             // 正值通常表示向电网输送功率 (发电/放电)，
                             // 负值表示从电网吸收功率 (消耗/充电)。
    double soc; // 设备的荷电状态 (State of Charge)，通常表示为0.0到1.0之间的小数 (即0%到100%)。
                // 对于电池储能，表示剩余电量百分比。

    // 构造函数
    // power: 初始功率 (kW)，默认为0.0。
    // s: 初始荷电状态 (SOC)，默认为0.5 (50%)。
    PhysicalStateComponent(double power = 0.0, double s = 0.5);
};

// 组件 (Component): 频率控制配置组件
// 存储设备参与一次频率调节 (Primary Frequency Regulation) 的配置参数。
struct FrequencyControlConfigComponent : public IComponent {
    // 设备类型枚举，用于区分不同类型的可调资源。
    enum class DeviceType {
        EV_PILE, // 电动汽车充电桩 (Electric Vehicle Charging Pile)
        ESS_UNIT // 储能单元 (Energy Storage System Unit)
    };

    DeviceType type; // 设备的具体类型。
    double base_power_kW; // 基准功率 (kW)。设备在系统频率正常 (无偏差) 时计划的出力或消耗功率。
    double gain_kW_per_Hz; // 调节增益或下垂系数 (kW/Hz)。表示每单位频率偏差 (Hz) 对应的功率调整量 (kW)。
    double deadband_Hz; // 频率死区 (Hz)。在此绝对频率偏差范围内，设备不响应频率变化。
    double max_output_kW; // 设备允许的最大输出功率 (kW) (例如，最大放电功率)。
    double min_output_kW; // 设备允许的最小输出功率 (kW) (通常为负值，表示最大充电功率或最小消耗功率)。
    double soc_min_threshold; // SOC最小允许阈值。当SOC低于此值时，可能会限制放电或强制充电。
    double soc_max_threshold; // SOC最大允许阈值。当SOC高于此值时，可能会限制充电或强制放电。

    // 构造函数
    FrequencyControlConfigComponent(DeviceType t, double base_p, double gain, double db,
        double max_p, double min_p,
        double soc_min = 0.0, double soc_max = 1.0);
};

// 函数：计算频率偏差
// 根据扰动发生后的相对时间 `t_relative` (单位：秒) 来计算系统频率的理论偏差值 (单位：Hz)。
// 这个函数通常基于一个简化的电力系统频率响应模型 (如单机等效模型或特定传递函数)。
// 返回计算得到的频率偏差值。
double calculate_frequency_deviation(double t_relative);

// 协程任务：频率预言机 (Frequency Oracle Task)
// 此协程模拟一个外部的“频率预言机”或频率测量单元。
// 它会根据 `calculate_frequency_deviation` 函数定义的模型，周期性地计算当前的系统频率偏差，
// 并通过触发 `FREQUENCY_UPDATE_EVENT` 事件，将此信息广播给仿真中的其他部分 (如VPP控制器)。
// registry: ECS注册表的引用，用于可能访问某些全局状态或实体信息 (在此例中主要用于统计总功率)。
// ev_entities: 包含所有电动汽车充电桩实体的向量。
// ess_entities: 包含所有储能单元实体的向量。
// disturbance_start_time_s: 系统发生频率扰动 (例如，发电机跳闸或负荷突变) 的仿真开始时间 (秒)。
// simulation_step_ms: 频率预言机更新和发布频率事件的时间步长 (毫秒)。
cps_coro::Task frequencyOracleTask(Registry& registry,
    const std::vector<Entity>& ev_entities,
    const std::vector<Entity>& ess_entities,
    double disturbance_start_time_s,
    double simulation_step_ms);

// 【旧的VPP任务声明，将被新的 individualDeviceFrequencyResponseTask 替代，此处保留或删除均可】
// 协程任务：虚拟电厂 (VPP) 频率响应任务
// cps_coro::Task vppFrequencyResponseTask(Registry& registry,
//     const std::string& vpp_name,
//     const std::vector<Entity>& managed_entities,
//     double simulation_step_ms_parameter);

// 协程任务：单个设备频率响应任务 (VPP中的独立设备)
// 此协程模拟VPP中单个设备 (如EV充电桩、ESS) 如何响应频率变化。
// 它会等待 FREQUENCY_UPDATE_EVENT 事件，获取当前的频率偏差信息。
// 然后，根据此设备的 FrequencyControlConfigComponent 和 PhysicalStateComponent，
// 计算并更新其功率输出 (或消耗)，以参与一次频率调节。
// registry: ECS注册表的引用，用于访问和修改此设备实体的组件数据。
// device_entity: 此协程管理的设备实体ID。
// device_log_name: 用于日志记录的设备名称或标识 (例如 "EV桩_1", "ESS单元_0")。
cps_coro::Task individualDeviceFrequencyResponseTask(Registry& registry, Entity device_entity, const std::string& device_log_name);

#endif // FREQUENCY_SYSTEM_H