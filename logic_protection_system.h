// logic_protection_system.h
#ifndef LOGIC_PROTECTION_SYSTEM_H
#define LOGIC_PROTECTION_SYSTEM_H

#include "cps_coro_lib.h" // 引入协程库
#include "ecs_core.h" // 引入实体组件系统核心库
#include "logging_utils.h" // 引入日志工具 (例如 g_console_logger)
#include "simulation_events_and_data.h" // 引入仿真中共享的事件ID和数据结构定义

#include <chrono> // 用于时间相关的操作
#include <cstdio> // 用于 snprintf
#include <iostream> // 用于备用日志输出 (如果g_console_logger未初始化)
#include <optional> // 用于可选的实体链接 (例如上下游线路)
#include <string> // C++标准字符串类型
#include <vector> // C++标准动态数组 (向量)

// --- 前向声明 ---
class Registry; // ECS注册表类
namespace cps_coro {
class Scheduler; // 协程调度器类
}

// --- 逻辑保护专用事件数据结构 ---

// 逻辑故障信息结构体
// 用于传递故障发生在线路上的信息。
struct LogicFaultInfo {
    Entity faulted_line_entity; // 发生故障的线路实体ID
};

// 逻辑断路器命令结构体
// 用于向特定断路器发送命令 (目前主要是跳闸命令)。
struct LogicBreakerCommand {
    Entity breaker_to_trip_entity; // 需要执行跳闸操作的断路器实体ID
};

// 逻辑断路器状态结构体
// 用于通告断路器状态的变更。
struct LogicBreakerStatus {
    Entity breaker_entity; // 状态发生变更的断路器实体ID
    bool is_open; // 断路器是否处于打开状态 (true表示打开, false表示闭合)
};

// --- 逻辑保护仿真专用组件 ---
// 这些组件用于存储逻辑保护仿真中各个实体的属性和状态。

// 线路标识组件
// 存储线路的基本信息，如名称、关联的断路器、以及上下游线路连接关系。
struct LineIdentityComponent : public IComponent {
    std::string name; // 线路名称 (例如 "线路A")
    std::optional<Entity> upstream_line_entity; // 上游线路的实体ID (可选)
    std::optional<Entity> downstream_line_entity; // 下游线路的实体ID (可选)
    Entity associated_breaker_entity; // 与此线路关联的、用于隔离此线路的断路器实体ID

    // 构造函数
    LineIdentityComponent(std::string n, Entity breaker_e,
        std::optional<Entity> up = std::nullopt,
        std::optional<Entity> down = std::nullopt)
        : name(std::move(n))
        , associated_breaker_entity(breaker_e)
        , upstream_line_entity(up)
        , downstream_line_entity(down)
    {
    }
};

// 断路器标识组件
// 存储断路器的基本信息，如名称、它负责隔离的线路、以及是否会发生拒动。
struct BreakerIdentityComponent : public IComponent {
    std::string name; // 断路器名称 (例如 "断路器A")
    Entity line_entity_it_isolates; // 此断路器主要负责隔离的线路实体ID
    bool is_stuck_on_trip_cmd = false; // 此断路器在收到跳闸命令时是否会拒动 (true表示会拒动)

    // 构造函数
    BreakerIdentityComponent(std::string n, Entity line_e, bool stuck = false)
        : name(std::move(n))
        , line_entity_it_isolates(line_e)
        , is_stuck_on_trip_cmd(stuck)
    {
    }
};

// 断路器状态组件
// 存储断路器的当前运行状态 (开断状态)。
struct BreakerStateComponent : public IComponent {
    bool is_open = false; // 断路器是否处于打开状态 (true表示打开, false表示闭合)。默认为闭合。

    // 构造函数
    BreakerStateComponent(bool open = false)
        : is_open(open)
    {
    }
};

// 保护装置组件
// 存储保护装置的详细信息，包括类型、状态、保护的线路、控制的断路器和动作延时。
struct ProtectionDeviceComponent : public IComponent {
    // 保护类型：主保护或后备保护
    enum class Type { MAIN,
        BACKUP };
    // 保护装置的内部运行状态
    enum class State {
        IDLE, // 空闲状态：等待故障
        PICKED_UP, // 启动状态：检测到相关故障，准备计时
        TRIPPING_TIMER_RUNNING, // 跳闸计时状态：正在进行延时计时
        TRIPPED, // 已跳闸状态：已发出跳闸命令
        RESETTING // 复归状态：故障消失或被隔离，正在返回空闲状态
    };

    std::string name; // 保护装置名称 (例如 "C线路主保护")
    Type type; // 保护类型 (主保护/后备保护)
    Entity protected_line_entity; // 此保护装置直接监视或负责的线路实体ID
    Entity commanded_breaker_entity; // 此保护装置在动作时将发出命令的断路器实体ID
    cps_coro::Scheduler::duration trip_delay; // 保护动作的延时时长 (从检测到故障到发出跳闸命令)
    State current_state = State::IDLE; // 当前保护装置的内部状态，默认为空闲
    std::optional<Entity> fault_currently_seen_on_line; // 当前保护装置检测到的、正在处理的故障所在的线路实体ID (可选)

    // 构造函数
    ProtectionDeviceComponent(std::string n, Type t, Entity p_line, Entity c_breaker, int delay_ms)
        : name(std::move(n))
        , type(t)
        , protected_line_entity(p_line)
        , commanded_breaker_entity(c_breaker)
        , trip_delay(std::chrono::milliseconds(delay_ms))
    {
    }
};

// --- 逻辑保护系统主类 ---
// LogicProtectionSystem 类封装了逻辑保护仿真的所有实体创建、场景设置和核心逻辑。
class LogicProtectionSystem {
public:
    // 构造函数
    LogicProtectionSystem(Registry& registry, cps_coro::Scheduler& scheduler);

    // 初始化仿真场景中的所有实体 (线路、断路器、保护装置)
    void initialize_scenario_entities();

    // 主仿真场景协程：模拟永久性故障及特定断路器拒动的场景
    cps_coro::Task simulate_permanent_fault_with_breaker_failure_scenario();

private:
    Registry& registry_; // ECS注册表实例的引用，用于管理实体和组件
    cps_coro::Scheduler& scheduler_; // 协程调度器实例的引用，用于管理时间和事件

    // 存储场景中关键实体的ID，方便后续访问
    Entity line_A_entity, line_B_entity, line_C_entity;
    Entity breaker_A_entity, breaker_B_entity, breaker_C_entity;
    Entity prot_main_A_entity, prot_backup_A_entity;
    Entity prot_main_B_entity, prot_backup_B_entity;
    Entity prot_main_C_entity; // C线路只有主保护，没有后备保护（根据场景描述）

    // 保护装置的逻辑行为协程任务
    cps_coro::Task protection_device_logic_task(Entity protection_entity);
    // 断路器的逻辑行为协程任务
    cps_coro::Task breaker_logic_task(Entity breaker_entity);

    // 拓扑检查辅助函数：判断目标线路 target_line_entity 是否是当前线路 current_line_entity 的下游或就是当前线路本身
    bool is_line_downstream_or_same(Entity current_line_entity, Entity target_line_entity);
    // 拓扑检查核心函数：从 perspective_line_entity 的视角（通常是保护安装位置的线路）判断，
    // 到达 fault_on_line_entity（故障发生的线路）的路径是否仍然带电（即路径上没有已打开的断路器）。
    bool is_fault_path_energized(Entity perspective_line_entity, Entity fault_on_line_entity);
};

#endif // LOGIC_PROTECTION_SYSTEM_H