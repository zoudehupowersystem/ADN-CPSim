// protection_system.h
// 定义了电力系统继电保护相关的组件、系统类和协程任务。
// 这个模块模拟保护装置 (如过流保护、距离保护) 如何检测故障并发出跳闸指令。
#ifndef PROTECTION_SYSTEM_H
#define PROTECTION_SYSTEM_H

#include "cps_coro_lib.h" // 协程库，用于定义异步任务 (cps_coro::Task) 和事件等待
#include "ecs_core.h" // ECS核心库，用于组件 (IComponent) 和实体 (Entity) 管理
#include "simulation_events_and_data.h" // 包含仿真中共享的事件ID (如故障事件) 和数据结构 (如FaultInfo)
#include <string> // C++标准字符串类型
#include <vector> // C++标准动态数组 (向量)

// 保护组件接口 (Protective Component Interface) - 继承自IComponent
// 这是一个抽象基类，定义了所有具体保护逻辑组件 (例如过流保护、距离保护)
// 必须实现的通用行为和接口。
class ProtectiveComp : public IComponent {
public:
    // 检查保护装置是否应启动 (pick up / operate) 响应给定的故障信息。
    // fault_data: 包含当前故障详细信息 (如电流、电压、阻抗) 的结构体。
    // self_entity_id: 当前这个保护组件实例所关联的被保护设备 (如线路、变压器) 的实体ID。
    //                 用于判断故障是否在本设备上，或用于方向性判断等。
    // 返回: 如果保护装置根据其定值和逻辑判断应该启动，则返回 true；否则返回 false。
    virtual bool pick_up(const FaultInfo& fault_data, Entity self_entity_id) = 0;

    // 获取在当前故障情况下，保护装置从启动到发出跳闸指令所需的延时时间 (毫秒)。
    // fault_data: 故障信息，某些保护类型 (如反时限过流保护) 的延时可能依赖于故障的严重程度。
    // 返回: 跳闸延时 (以毫秒为单位)。对于瞬时动作的保护，可以返回0。
    virtual int trip_delay_ms(const FaultInfo& fault_data) const = 0;

    // 获取此保护装置或保护段的名称或标识符。
    // 主要用于日志记录和调试，方便识别是哪个保护装置在动作。
    // 返回: 指向描述保护名称的C风格字符串的指针。
    virtual const char* name() const = 0;

    // 虚析构函数，确保通过基类指针正确销毁派生类对象。
    virtual ~ProtectiveComp() = default;
};

// 过电流保护组件 (OverCurrent Protection Component)
// 实现一个简单的定时限过电流保护逻辑。
class OverCurrentProtection : public ProtectiveComp {
public:
    // 构造函数
    // pickup_current_kA: 过电流保护的启动电流定值 (单位：kA)。当故障电流超过此值时，保护启动。
    // delay_ms: 固定动作延时 (单位：毫秒)。保护启动后，经过此延时发出跳闸指令。
    // stage_name: 此过流保护段的名称 (例如 "OC-速断段", "OC-定时限段", "OC-反时限段")，用于日志。
    OverCurrentProtection(double pickup_current_kA, int delay_ms, std::string stage_name = "过流保护段");

    bool pick_up(const FaultInfo& fault_data, Entity self_entity_id) override;
    int trip_delay_ms(const FaultInfo& fault_data) const override;
    const char* name() const override;

private:
    double pickup_current_kA_; // 启动电流阈值 (kA)
    int fixed_delay_ms_; // 固定动作延时 (毫秒)
    std::string stage_name_; // 保护段的名称，便于识别
};

// 距离保护组件 (Distance Protection Component)
// 实现一个简化的多段距离保护逻辑。
class DistanceProtection : public ProtectiveComp {
public:
    // 构造函数
    // z1_ohm, t1_ms: 第I段阻抗定值 (欧姆) 和对应延时 (毫秒)。通常第I段延时为0 (瞬时)。
    // z2_ohm, t2_ms: 第II段阻抗定值 (欧姆) 和对应延时 (毫秒)。
    // z3_ohm, t3_ms: 第III段阻抗定值 (欧姆) 和对应延时 (毫秒)。
    DistanceProtection(double z1_ohm, int t1_ms, double z2_ohm, int t2_ms, double z3_ohm, int t3_ms);

    bool pick_up(const FaultInfo& fault_data, Entity self_entity_id) override;
    int trip_delay_ms(const FaultInfo& fault_data) const override;
    const char* name() const override;

private:
    std::vector<double> z_set_; // 存储各段阻抗定值的向量 (z_set_[0]为第I段, z_set_[1]为第II段, ...)
    std::vector<int> t_ms_; // 存储各段对应延时的向量 (t_ms_[0]为第I段延时, ...)
};

// 保护系统类 (Protection System Class)
// 负责管理整个保护系统的运行逻辑，例如监听故障事件、协调各保护组件的判断和动作。
class ProtectionSystem {
public:
    // 构造函数，显式传递ECS注册表和调度器的引用。
    // 这种依赖注入的方式比使用全局变量更易于测试和维护。
    ProtectionSystem(Registry& reg, cps_coro::Scheduler& sch);

    // 运行保护系统的主要协程任务。
    // 此任务通常会无限循环，等待故障事件，然后分发给相关的保护组件处理。
    cps_coro::Task run();

    // 向系统中注入一个故障信息。
    // 这通常由外部的故障发生器或仿真场景控制器调用。
    void inject_fault(const FaultInfo& info);

private:
    // 内部协程任务：在指定的延时后，代表某个保护组件执行实际的跳闸动作 (即触发跳闸事件)。
    // protected_entity_id: 将要被跳闸的设备实体ID。
    // delay_ms: 跳闸延时。
    // protection_name: 发出跳闸指令的保护装置名称。
    // actual_faulty_entity_id: 实际发生故障的设备实体ID (可能与 protected_entity_id 不同，例如后备保护)。
    cps_coro::Task trip_later(Entity protected_entity_id, int delay_ms, const char* protection_name, Entity actual_faulty_entity_id);

    Registry& registry_; // 对ECS注册表的引用，用于访问保护组件。
    cps_coro::Scheduler& scheduler_; // 对调度器的引用，用于事件触发和延时。
};

// 协程任务：故障注入器 (专用于保护系统测试)
// 此任务按预设的时间顺序向保护系统注入模拟的故障。
// protSystem: 对 ProtectionSystem 实例的引用。
// line1_id, transformer1_id: 示例中被指定为故障点的实体ID。
// scheduler: 对调度器的引用，主要用于获取当前仿真时间进行日志记录，或用于内部延时 (尽管此任务的延时通常用 cps_coro::delay)。
cps_coro::Task faultInjectorTask_prot(ProtectionSystem& protSystem, Entity line1_id, Entity transformer1_id, cps_coro::Scheduler& scheduler);

// 协程任务：断路器代理 (专用于保护系统测试)
// 模拟一个与特定被保护设备 (associated_entity_id) 关联的断路器的行为。
// 它会监听跳闸事件 (ENTITY_TRIP_EVENT_PROT)，如果事件是针对其关联设备的，则模拟断路器断开动作。
// associated_entity_id: 此断路器代理所关联的设备实体ID。
// entity_name: 关联设备的名称 (用于日志)。
// scheduler: 对调度器的引用。
cps_coro::Task circuitBreakerAgentTask_prot(Entity associated_entity_id, const std::string& entity_name, cps_coro::Scheduler& scheduler);

#endif // PROTECTION_SYSTEM_H