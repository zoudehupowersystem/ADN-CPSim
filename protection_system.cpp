// protection_system.cpp
// 实现了继电保护系统相关的类方法和协程任务逻辑。
#include "protection_system.h"
#include "logging_utils.h" // 引入日志工具，用于 g_console_logger
#include <chrono> // C++时间库，主要用于 cps_coro::Scheduler::duration

// 全局调度器指针 - 假设在 main.cpp 中定义并初始化。
// 注意：在大型项目中，更推荐通过依赖注入（例如将调度器引用作为构造函数参数传递）来管理调度器访问，
// 而不是依赖全局变量，以提高代码的模块化和可测试性。
extern cps_coro::Scheduler* g_scheduler;

// OverCurrentProtection 类成员函数实现

// 构造函数
OverCurrentProtection::OverCurrentProtection(double pickup_current_kA, int delay_ms, std::string stage_name)
    : pickup_current_kA_(pickup_current_kA) // 初始化启动电流定值
    , fixed_delay_ms_(delay_ms) // 初始化固定动作延时
    , stage_name_(std::move(stage_name)) // 初始化保护段名称 (使用 std::move 提高效率)
{
}

// 判断是否启动 (pick up)
bool OverCurrentProtection::pick_up(const FaultInfo& fault_data, Entity /*self_entity_id*/)
{
    // 对于简单的过流保护，仅当故障电流大于或等于设定的启动电流定值时，保护启动。
    // self_entity_id 在此简单实现中未使用，但在更复杂的保护（如方向过流）中可能需要。
    return fault_data.current_kA >= pickup_current_kA_;
}

// 获取跳闸延时
int OverCurrentProtection::trip_delay_ms(const FaultInfo& /*fault_data*/) const
{
    // 对于定时限过流保护，直接返回在构造时设定的固定延时。
    // fault_data 在此简单实现中未使用，但在反时限过流保护中，延时会依赖于故障电流的大小。
    return fixed_delay_ms_;
}

// 获取保护名称
const char* OverCurrentProtection::name() const
{
    // 返回此过流保护段的名称字符串。
    return stage_name_.c_str();
}

// DistanceProtection 类成员函数实现

// 构造函数
DistanceProtection::DistanceProtection(double z1_ohm, int t1_ms, double z2_ohm, int t2_ms, double z3_ohm, int t3_ms)
    : z_set_ { z1_ohm, z2_ohm, z3_ohm } // 使用初始化列表初始化各段阻抗定值
    , t_ms_ { t1_ms, t2_ms, t3_ms } // 使用初始化列表初始化各段对应延时
{
}

// 判断是否启动 (pick up)
bool DistanceProtection::pick_up(const FaultInfo& fault_data, Entity self_entity_id)
{
    // 距离保护的启动逻辑：
    // 1. 检查故障是否发生在被保护元件 (self_entity_id) 之外的远方 (例如，作为相邻元件的后备保护)。
    //    faulty_entity_id == 0 可能表示故障位置未知或为系统级故障，此时可能按本元件故障或特定策略处理。
    //    此处简化：如果故障明确在其他元件上 (faulty_entity_id != self_entity_id 且非0)，
    //    则仅当故障阻抗落入最远的一段 (例如第III段，z_set_[2]) 时，作为后备启动。
    if (fault_data.faulty_entity_id != self_entity_id && fault_data.faulty_entity_id != 0) {
        return fault_data.impedance_Ohm <= z_set_[2]; // 测量阻抗小于等于第III段定值
    }
    // 2. 如果故障发生在本元件上 (faulty_entity_id == self_entity_id) 或位置不确定 (faulty_entity_id == 0)，
    //    则检查测量阻抗是否落入任何一段 (I, II, 或 III段) 的保护范围之内。
    return fault_data.impedance_Ohm <= z_set_[0] || // 是否落入第I段
        fault_data.impedance_Ohm <= z_set_[1] || // 或是否落入第II段
        fault_data.impedance_Ohm <= z_set_[2]; // 或是否落入第III段
}

// 获取跳闸延时
int DistanceProtection::trip_delay_ms(const FaultInfo& fault_data) const
{
    // 根据故障测量阻抗落在哪一段来确定相应的跳闸延时。
    // 顺序判断，因为保护范围是嵌套的 (第I段范围最小，第III段最大)。
    if (fault_data.impedance_Ohm <= z_set_[0]) // 如果落入第I段 (通常是瞬时动作)
        return t_ms_[0];
    if (fault_data.impedance_Ohm <= z_set_[1]) // 如果落入第II段 (且未落入第I段)
        return t_ms_[1];
    if (fault_data.impedance_Ohm <= z_set_[2]) // 如果落入第III段 (且未落入前两段)
        return t_ms_[2];

    // 如果测量阻抗不满足任何段的条件 (理论上 pick_up 函数会先进行判断，所以此路径不应轻易到达)，
    // 返回一个非常大的延时值，表示不跳闸或错误状态。
    return 99999;
}

// 获取保护名称
const char* DistanceProtection::name() const { return "距离保护"; } // 返回保护类型的通用名称

// ProtectionSystem 类成员函数实现

// 构造函数
ProtectionSystem::ProtectionSystem(Registry& reg, cps_coro::Scheduler& sch)
    : registry_(reg) // 初始化ECS注册表引用
    , scheduler_(sch) // 初始化调度器引用 (通过依赖注入传入)
{
}

// 注入故障信息
void ProtectionSystem::inject_fault(const FaultInfo& info)
{
    // 通过调度器触发一个 FAULT_INFO_EVENT_PROT 类型的事件，
    // 将故障信息 `info` 广播给系统中所有监听此事件的协程 (主要是 ProtectionSystem::run 任务)。
    scheduler_.trigger_event(FAULT_INFO_EVENT_PROT, info);
}

// 保护系统主运行协程
cps_coro::Task ProtectionSystem::run()
{
    if (g_console_logger) // 使用全局日志记录器 (或者可以通过 scheduler_ 引用获取当前时间)
        g_console_logger->info("[{}毫秒] [保护系统] 主任务已激活，正在等待 FAULT_INFO_EVENT_PROT (故障信息) 事件。",
            scheduler_.now().time_since_epoch().count()); // 使用 ProtectionSystem 自己的 scheduler_ 引用获取时间

    while (true) { // 无限循环，持续监听和处理故障事件
        // 协程等待 (挂起)，直到 FAULT_INFO_EVENT_PROT 事件被触发，并接收事件数据 (FaultInfo 类型)。
        auto fault_data = co_await cps_coro::wait_for_event<FaultInfo>(FAULT_INFO_EVENT_PROT);
        fault_data.calculate_impedance_if_needed(); // 如果故障信息中阻抗未提供，尝试根据电压电流计算

        if (g_console_logger)
            g_console_logger->info("[{}毫秒] [保护系统] 收到故障信息事件。故障发生在实体 #{}, 故障电流: {:.2f} kA, 计算阻抗: {:.2f} Ohm, 故障距离: {:.1f} km。",
                scheduler_.now().time_since_epoch().count(),
                fault_data.faulty_entity_id, fault_data.current_kA,
                fault_data.impedance_Ohm, fault_data.distance_km);

        // 遍历注册表 (registry_) 中所有类型为 ProtectiveComp (保护组件基类) 的组件实例。
        // 对于每个找到的保护组件，执行 lambda 函数中的逻辑。
        registry_.for_each<ProtectiveComp>([&](ProtectiveComp& comp, Entity entity_id) {
            // 调用具体保护组件的 pick_up 方法，判断其是否针对当前故障启动。
            // entity_id 是当前这个保护组件实例所关联的被保护设备的ID。
            if (comp.pick_up(fault_data, entity_id)) {
                int delay_ms = comp.trip_delay_ms(fault_data); // 如果启动，获取其计算出的跳闸延时。
                if (g_console_logger)
                    g_console_logger->info("[{}毫秒] [保护组件-{}] (实体ID #{}) 已启动 (PICKED UP)。计算得到的跳闸延时: {} 毫秒。",
                        scheduler_.now().time_since_epoch().count(),
                        comp.name(), entity_id, delay_ms);

                // 创建并分离一个新的子协程任务 (trip_later)，用于在延时后执行实际的跳闸逻辑。
                // 这使得主保护逻辑可以继续处理其他组件或事件，而不会阻塞。
                auto sub_task = trip_later(entity_id, delay_ms, comp.name(), fault_data.faulty_entity_id);
                sub_task.detach(); // 分离子任务，让其在调度器中独立运行，主任务不等待其完成。
            }
        });
    } // 循环继续，等待下一个故障事件
}

// 延时跳闸子协程任务
cps_coro::Task ProtectionSystem::trip_later(Entity protected_entity_id, int delay_ms, const char* protection_name, Entity actual_faulty_entity_id)
{
    // 协程等待 (挂起)，直到指定的 `delay_ms` 毫秒延时过去。
    co_await cps_coro::delay(cps_coro::Scheduler::duration(delay_ms));

    // 延时到达后，执行跳闸动作：
    if (g_console_logger)
        g_console_logger->info("[{}毫秒] [保护组件-{}] (实体ID #{}) => 执行跳闸指令！(针对发生在实体 #{} 上的故障)",
            scheduler_.now().time_since_epoch().count(), // 确保这里使用的是成员变量 scheduler_ 来获取时间
            protection_name, protected_entity_id, actual_faulty_entity_id);

    // 通过调度器触发 ENTITY_TRIP_EVENT_PROT 类型的事件，
    // 将被跳闸的设备实体ID (protected_entity_id) 作为事件数据广播出去。
    // 其他系统（如断路器代理协程）可以监听此事件并作出相应动作。
    scheduler_.trigger_event(ENTITY_TRIP_EVENT_PROT, protected_entity_id);
}

// faultInjectorTask_prot 协程任务实现 (故障注入器 - 保护系统专用)
cps_coro::Task faultInjectorTask_prot(ProtectionSystem& protSystem, Entity line1_id, Entity transformer1_id, cps_coro::Scheduler& scheduler_ref)
{
    // 第一次故障注入
    co_await cps_coro::delay(cps_coro::Scheduler::duration(6000)); // 延时6秒 (仿真时间)
    FaultInfo fault1;
    fault1.faulty_entity_id = line1_id; // 故障设定在线路1 (line1_id) 上
    fault1.current_kA = 15.0; // 故障电流设为 15.0 kA
    fault1.voltage_kV = 220.0; // 故障时电压 (或故障前额定电压) 220 kV
    fault1.distance_km = 10.0; // 故障距离线路始端 10.0 km
    fault1.impedance_Ohm = (220.0 / 15.0) * 0.8; // 示例性地计算一个故障阻抗值
    if (g_console_logger) // 使用 scheduler_ref 获取当前时间
        g_console_logger->info("[{}毫秒] [故障注入器_保护] 正在向线路实体 #{} 注入故障场景 #1。",
            scheduler_ref.now().time_since_epoch().count(), line1_id);
    protSystem.inject_fault(fault1); // 调用ProtectionSystem的方法注入此故障信息

    // 第二次故障注入
    co_await cps_coro::delay(cps_coro::Scheduler::duration(7000)); // 再延时7秒 (即总计13秒后)
    FaultInfo fault2;
    fault2.faulty_entity_id = transformer1_id; // 故障设定在变压器1 (transformer1_id) 上
    fault2.current_kA = 3.0; // 故障电流设为 3.0 kA
    fault2.voltage_kV = 220.0; // 额定电压 220 kV
    // 此处 impedance_Ohm 未手动设置，将由 fault2.calculate_impedance_if_needed() 自动计算
    fault2.calculate_impedance_if_needed();
    if (g_console_logger)
        g_console_logger->info("[{}毫秒] [故障注入器_保护] 正在向变压器实体 #{} 注入故障场景 #2。",
            scheduler_ref.now().time_since_epoch().count(), transformer1_id);
    protSystem.inject_fault(fault2);

    co_return; // 故障注入任务完成
}

// circuitBreakerAgentTask_prot 协程任务实现 (断路器代理 - 保护系统专用)
cps_coro::Task circuitBreakerAgentTask_prot(Entity associated_entity_id, const std::string& entity_name, cps_coro::Scheduler& scheduler_ref)
{
    if (g_console_logger)
        g_console_logger->info("[{}毫秒] [断路器代理_保护-{}-实体#{}] 任务已激活，正在等待 ENTITY_TRIP_EVENT_PROT (设备跳闸) 事件。",
            scheduler_ref.now().time_since_epoch().count(), entity_name, associated_entity_id);

    while (true) { // 无限循环，持续监听跳闸事件
        // 协程等待 (挂起)，直到 ENTITY_TRIP_EVENT_PROT 事件被触发，并获取事件数据 (即被跳闸的实体ID)。
        Entity tripped_entity_id = co_await cps_coro::wait_for_event<Entity>(ENTITY_TRIP_EVENT_PROT);

        // 检查被跳闸的实体ID是否是此断路器代理所关联的设备ID。
        if (tripped_entity_id == associated_entity_id) {
            if (g_console_logger)
                g_console_logger->info("[{}毫秒] [断路器代理_保护-{}-实体#{}] 收到针对本设备的跳闸指令。",
                    scheduler_ref.now().time_since_epoch().count(), entity_name, associated_entity_id);

            // 模拟断路器实际操作所需的时间
            co_await cps_coro::delay(cps_coro::Scheduler::duration(100)); // 假设断路器操作延时为100毫秒

            if (g_console_logger)
                g_console_logger->info("[{}毫秒] [断路器代理_保护-{}-实体#{}] 断路器已成功断开 (OPENED)。",
                    scheduler_ref.now().time_since_epoch().count(), entity_name, associated_entity_id);

            // （可选）触发一个更通用的 BREAKER_OPENED_EVENT 事件，
            // 将此断路器已断开的信息通知给系统中其他可能关心此状态的模块（如拓扑分析、潮流计算等）。
            // 事件数据可以是断开的断路器（或其关联设备）的实体ID。
            scheduler_ref.trigger_event(BREAKER_OPENED_EVENT, associated_entity_id);
        }
    } // 循环继续，等待下一个跳闸事件
}