// test_model.cpp
//  包含自动电压控制 (AVC) 相关的复杂仿真场景定义与测试函数。
//  这个文件演示了如何使用协程库构建一个包含传感器、控制器和监测器的多智能体仿真。
#include "cps_coro_lib.h" // 核心协程库
#include "ecs_core.h" // 实体组件系统核心
#include "simulation_events_and_data.h" // 共享的事件ID和数据结构
#include <iomanip> // 用于 std::fixed 和 std::setprecision，以控制浮点数输出格式
#include <iostream> // 用于标准输入输出流 (主要是 std::cout)
#include <string> // 用于 std::string (例如在 LoadDataAvc 结构体中)

// 电压数据结构体 (用于 VOLTAGE_CHANGE_EVENT_AVC)
struct VoltageDataAvc {
    double voltage; // 电压值 (通常为标幺值, Per Unit, pu)
    cps_coro::Scheduler::time_point timestamp; // 事件发生的仿真时间戳
};

// 负荷数据结构体 (用于 LOAD_CHANGE_EVENT_AVC)
struct LoadDataAvc {
    double load_mw; // 负荷的有功功率值 (单位：兆瓦, MW)
    std::string bus_id; // 发生负荷变化的母线或区域的标识符 (字符串)
    cps_coro::Scheduler::time_point timestamp; // 事件发生的仿真时间戳
};

// --- 工具函数：获取当前仿真时间 (毫秒, AVC场景专用) ---
// scheduler: 对当前调度器实例的引用。
// 返回从调度器内部时钟的纪元 (epoch) 开始计数的毫秒数。
long long current_sim_ms_avc(cps_coro::Scheduler& scheduler)
{
    return scheduler.now().time_since_epoch().count();
}

// --- 复杂仿真场景的协程定义 ---

// Sensor 协程 (AVC场景)：模拟传感器周期性地或在特定条件下检测电压和负荷变化，并触发相应事件。
// scheduler: 对当前调度器实例的引用。
cps_coro::Task sensor_coroutine_complex_avc(cps_coro::Scheduler& scheduler)
{
    // 设置后续 std::cout 输出浮点数时，使用固定小数位数表示法，并保留两位小数。
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器 (复杂版): 初始化完成。系统当前处于标称运行状态。" << std::endl;

    // 场景 0: 宣布初始稳定状态
    co_await cps_coro::delay(std::chrono::seconds(1)); // 等待1仿真秒，让系统“稳定”或用于演示时序
    VoltageDataAvc initial_voltage = { 1.00, scheduler.now() }; // 初始电压为1.00 pu
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, initial_voltage); // 触发初始电压事件
    LoadDataAvc initial_load = { 100.0, "母线A", scheduler.now() }; // 初始负荷为100 MW，在母线A
    scheduler.trigger_event(LOAD_CHANGE_EVENT_AVC, initial_load); // 触发初始负荷事件
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 发布初始状态：电压 = " << initial_voltage.voltage
              << " pu, 负荷 = " << initial_load.load_mw << " MW (于 " << initial_load.bus_id << ")" << std::endl;

    // 场景 1: 负荷增加
    co_await cps_coro::delay(std::chrono::seconds(4)); // 再等待4仿真秒 (总仿真时间到达 1+4=5秒)
    LoadDataAvc load_increase1 = { 150.0, "母线A", scheduler.now() }; // 母线A负荷增加到150 MW
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 于 " << load_increase1.bus_id << " 检测到负荷增加。"
              << " 当前负荷 = " << load_increase1.load_mw << " MW。" << std::endl;
    scheduler.trigger_event(LOAD_CHANGE_EVENT_AVC, load_increase1);

    // 场景 2: 因负荷增加导致电压跌落
    co_await cps_coro::delay(std::chrono::seconds(2)); // 再等待2仿真秒 (总仿真时间 5+2=7秒)
    VoltageDataAvc voltage_dip1 = { 0.93, scheduler.now() }; // 电压跌落至0.93 pu
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 检测到电压发生跌落。当前电压 = " << voltage_dip1.voltage << " pu。" << std::endl;
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, voltage_dip1);

    // 场景 3: 另一母线上的负荷也增加
    co_await cps_coro::delay(std::chrono::seconds(5)); // 再等待5仿真秒 (总仿真时间 7+5=12秒)
    LoadDataAvc load_increase2 = { 80.0, "母线B", scheduler.now() }; // 母线B上出现新的负荷80 MW
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 于 " << load_increase2.bus_id << " 检测到负荷增加。"
              << " 当前负荷 = " << load_increase2.load_mw << " MW。" << std::endl;
    scheduler.trigger_event(LOAD_CHANGE_EVENT_AVC, load_increase2);

    // 场景 4: 因总体负荷较高导致更严重的电压跌落
    co_await cps_coro::delay(std::chrono::seconds(3)); // 再等待3仿真秒 (总仿真时间 12+3=15秒)
    VoltageDataAvc voltage_dip2 = { 0.88, scheduler.now() }; // 电压进一步跌落至0.88 pu (严重欠压)
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 检测到严重电压跌落。当前电压 = " << voltage_dip2.voltage << " pu。" << std::endl;
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, voltage_dip2);

    // 场景 5: 负荷显著下降 (例如，部分工业负荷切除或需求响应)
    co_await cps_coro::delay(std::chrono::seconds(5)); // 再等待5仿真秒 (总仿真时间 15+5=20秒)
    LoadDataAvc load_drop = { 70.0, "母线A", scheduler.now() }; // 母线A负荷下降至70 MW
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 于 " << load_drop.bus_id << " 检测到负荷显著下降。"
              << " 当前负荷 = " << load_drop.load_mw << " MW。" << std::endl;
    scheduler.trigger_event(LOAD_CHANGE_EVENT_AVC, load_drop);

    // 场景 6: 因负荷下降，电压部分恢复
    co_await cps_coro::delay(std::chrono::seconds(2)); // 再等待2仿真秒 (总仿真时间 20+2=22秒)
    VoltageDataAvc voltage_recover1 = { 0.97, scheduler.now() }; // 电压恢复至0.97 pu
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 电压已部分恢复。当前电压 = " << voltage_recover1.voltage << " pu。" << std::endl;
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, voltage_recover1);

    // 场景 7: 可能由于无功补偿装置的调节或进一步的负荷变化导致电压过冲
    co_await cps_coro::delay(std::chrono::seconds(3)); // 再等待3仿真秒 (总仿真时间 22+3=25秒)
    VoltageDataAvc voltage_overshoot = { 1.08, scheduler.now() }; // 电压过冲至1.08 pu (过压)
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 检测到电压发生过冲。当前电压 = " << voltage_overshoot.voltage << " pu。" << std::endl;
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, voltage_overshoot);

    // 场景 8: 电压最终稳定在正常范围
    co_await cps_coro::delay(std::chrono::seconds(5)); // 再等待5仿真秒 (总仿真时间 25+5=30秒)
    VoltageDataAvc voltage_stable = { 1.01, scheduler.now() }; // 电压稳定在1.01 pu
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 电压已稳定。当前电压 = " << voltage_stable.voltage << " pu。" << std::endl;
    scheduler.trigger_event(VOLTAGE_CHANGE_EVENT_AVC, voltage_stable);

    // 仿真序列结束前的最后等待，确保其他协程有足够时间处理最后一个事件
    co_await cps_coro::delay(std::chrono::seconds(5)); // 再等待5秒
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC传感器: 仿真序列已完成，任务即将关闭。" << std::endl;
    // 协程正常结束
}

// AVC Controller 协程 (AVC场景)：模拟自动电压控制器的行为。
// 它等待电压变化事件，并根据预设的控制逻辑采取相应措施。
// scheduler: 对当前调度器实例的引用。
cps_coro::Task avc_coroutine_complex_avc(cps_coro::Scheduler& scheduler)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器 (复杂版): 初始化完成。正在等待电压变化事件。" << std::endl;
    int event_count = 0; // 用于计数已处理的电压事件数量
    constexpr int max_events_to_process = 6; // 设定在此演示中，AVC控制器最多主动响应6个电压事件后结束任务

    try {
        while (event_count < max_events_to_process) {
            // 协程等待 (挂起)，直到 VOLTAGE_CHANGE_EVENT_AVC 事件被触发，并接收事件数据 (VoltageDataAvc 类型)。
            VoltageDataAvc data = co_await cps_coro::wait_for_event<VoltageDataAvc>(VOLTAGE_CHANGE_EVENT_AVC);
            event_count++; // 处理的事件数加一

            std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 收到第 " << event_count << " 个电压变化事件 (共处理 " << max_events_to_process << " 个)。"
                      << " 当前电压 = " << data.voltage << " pu (该事件发生于仿真时间: " << data.timestamp.time_since_epoch().count() << "毫秒)。" << std::endl;

            // 根据接收到的电压值，执行不同的控制逻辑
            if (data.voltage < 0.90) { // 电压严重过低 (例如，低于0.90 pu)
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 动作 -> 检测到严重低电压！建议：紧急投入主电容器组，并向调度运行人员发出告警。" << std::endl;
            } else if (data.voltage < 0.95) { // 电压一般过低 (例如，0.90 pu <= V < 0.95 pu)
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 动作 -> 检测到电压偏低。建议：投入备用电容器组或调整变压器分接头以升高电压。" << std::endl;
            } else if (data.voltage > 1.10) { // 电压严重过高 (例如，高于1.10 pu)
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 动作 -> 检测到严重高电压！建议：紧急切除主电容器组，并向调度运行人员发出告警。" << std::endl;
            } else if (data.voltage > 1.05) { // 电压一般过高 (例如，1.05 pu < V <= 1.10 pu)
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 动作 -> 检测到电压偏高。建议：切除部分电容器组或调整变压器分接头以降低电压。" << std::endl;
            } else { // 电压在正常范围 (例如，0.95 pu <= V <= 1.05 pu)
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 动作 -> 当前电压在正常范围内。继续监测，无需立即调整。" << std::endl;
            }
            // 模拟AVC设备实际动作或控制逻辑计算所需的时间延迟
            co_await cps_coro::delay(std::chrono::milliseconds(300)); // 假设AVC响应和执行一个控制动作需要300毫秒的仿真时间
        }
    } catch (const std::exception& e) {
        // 捕获并报告在协程执行过程中可能发生的任何标准库异常
        std::cerr << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器协程发生异常: " << e.what() << std::endl;
    }
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] AVC控制器: 已处理 " << event_count << " 个电压事件。任务即将结束。" << std::endl;
    // 协程正常结束 (达到最大处理事件数)
}

// Load Monitor 协程 (AVC场景)：模拟一个负荷监测器。
// 它等待负荷变化事件，并记录或响应这些变化。
// scheduler: 对当前调度器实例的引用。
cps_coro::Task load_monitor_coroutine_avc(cps_coro::Scheduler& scheduler)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器 (AVC场景): 初始化完成。正在等待负荷变化事件。" << std::endl;
    int event_count = 0;
    constexpr int max_events_to_process = 4; // 在此演示中，负荷监测器最多处理4个负荷事件后结束任务

    try {
        while (event_count < max_events_to_process) {
            // 协程等待 (挂起)，直到 LOAD_CHANGE_EVENT_AVC 事件被触发，并接收事件数据 (LoadDataAvc 类型)。
            LoadDataAvc data = co_await cps_coro::wait_for_event<LoadDataAvc>(LOAD_CHANGE_EVENT_AVC);
            event_count++;

            std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器: 收到第 " << event_count << " 个负荷变化事件 (共处理 " << max_events_to_process << " 个) "
                      << "于 " << data.bus_id << "。当前负荷 = " << data.load_mw << " MW (该事件发生于仿真时间: " << data.timestamp.time_since_epoch().count() << "毫秒)。" << std::endl;

            // 根据负荷数据进行简单判断和响应 (示例)
            if (data.load_mw > 140.0) {
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器: 告警 -> 于 " << data.bus_id << " 检测到高负荷状态！" << std::endl;
            } else if (data.load_mw < 80.0 && data.bus_id == "母线A") {
                std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器: 信息 -> 于 " << data.bus_id << " 负荷已显著降低。" << std::endl;
            }
            // 负荷监测器通常是被动的，记录信息或发出告警，此处不添加额外的处理延迟。
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器协程发生异常: " << e.what() << std::endl;
    }
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 负荷监测器: 已处理 " << event_count << " 个负荷事件。任务即将结束。" << std::endl;
    // 协程正常结束
}

// --- 测试函数 ---

// 增强的非实时复杂仿真测试函数 (AVC场景)
// 此函数设置并运行一个使用标准 Scheduler 的非实时仿真。
void avc_test_non_realtime()
{
    std::cout << "\n--- 开始 AVC 复杂场景仿真 (非实时模式) ---" << std::endl;

    cps_coro::Scheduler scheduler; // 创建标准事件调度器实例

    // 启动各个协程任务
    // .detach() 方法使得任务启动后，其生命周期由协程库的调度器管理，
    // 主控制流 (即 avc_test_non_realtime 函数) 不直接等待这些任务完成。
    // 如果希望主控制流能明确等待或检查任务状态，则不应调用 detach()。
    cps_coro::Task sensor_task = sensor_coroutine_complex_avc(scheduler);
    sensor_task.detach(); // 演示分离任务，让传感器任务独立运行
    cps_coro::Task avc_task = avc_coroutine_complex_avc(scheduler);
    avc_task.detach(); // AVC控制器任务也分离
    cps_coro::Task load_monitor_task = load_monitor_coroutine_avc(scheduler);
    load_monitor_task.detach(); // 负荷监测器任务也分离

    // 设定仿真的总时长 (仿真时间)
    auto simulation_duration = std::chrono::seconds(40); // 计划仿真运行40个仿真秒
    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 主程序 (非实时模式): 准备启动调度器。计划仿真持续时间 "
              << simulation_duration.count() << " 秒 (仿真时间)。" << std::endl;

    // 运行调度器：
    // scheduler.run_until() 会执行事件循环，处理就绪任务和定时任务，
    // 直到调度器的当前仿真时间达到 (scheduler.now() + simulation_duration)，
    // 或者所有任务都已完成且没有待处理的定时事件（以先到者为准）。
    scheduler.run_until(scheduler.now() + simulation_duration);

    std::cout << "[" << current_sim_ms_avc(scheduler) << "毫秒] 主程序 (非实时模式): 调度器运行结束 (已达到预设仿真时间或无更多任务)。" << std::endl;

    // 对于已分离 (detached) 的任务，调用 is_done() 可能不总能准确反映其最终状态，
    // 特别是如果检查发生在调度器已停止对其进行主动管理之后。
    // 通常，分离的任务意味着主逻辑不直接关心其完成与否，它们会自行运行至结束。
    // 如果确实需要精确跟踪任务完成状态，则不应分离它们，并在 run_until 后检查。
    // 此处的检查更多是为演示API，并提示其在分离任务情境下的局限性。
    // 协程内部的结束日志是
    // 检查协程任务是否完成（注意：对于已 detach 的任务，此检查的意义有限，
    // 因为主逻辑通常不直接等待它们。协程内部的结束日志是更直接的完成指标。

    std::cout << "--- 非实时模式AVC复杂场景仿真结束 ---" << std::endl;
}

// 新增的实时复杂仿真测试函数 (AVC场景)
// 此函数设置并运行一个使用 RealTimeScheduler 的实时仿真。
// 在实时仿真中，调度器会尝试使仿真时间的推进与物理时钟（挂钟时间）的流逝同步。
void avc_test_realtime()
{
    std::cout << "\n--- 开始 AVC 复杂场景仿真 (实时模式) ---" << std::endl;
    std::cout << "(提示: 此仿真将尝试花费大约 40 秒的真实物理时间来运行)" << std::endl;

    cps_coro::RealTimeScheduler rt_scheduler; // 创建实时调度器实例

    // 启动各个协程任务，同样使用 detach 使其独立运行
    cps_coro::Task sensor_task_rt = sensor_coroutine_complex_avc(rt_scheduler);
    sensor_task_rt.detach();
    cps_coro::Task avc_task_rt = avc_coroutine_complex_avc(rt_scheduler);
    avc_task_rt.detach();
    cps_coro::Task load_monitor_task_rt = load_monitor_coroutine_avc(rt_scheduler);
    load_monitor_task_rt.detach();

    // 设定仿真的总时长 (仿真时间)
    auto simulation_duration = std::chrono::seconds(40); // 计划仿真运行40个仿真秒
    std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): 准备启动实时调度器。计划仿真持续时间 "
              << simulation_duration.count() << " 秒 (仿真时间将与真实物理时间同步)。" << std::endl;

    auto wall_clock_start = std::chrono::steady_clock::now(); // 记录真实物理时间的开始时刻

    // 运行实时调度器：
    // rt_scheduler.run_real_time_until() 会执行事件循环，并尝试通过插入延时 (std::this_thread::sleep_for)
    // 来确保仿真时间的推进速度与真实物理时间的流逝速度一致。
    rt_scheduler.run_real_time_until(rt_scheduler.now() + simulation_duration);

    auto wall_clock_end = std::chrono::steady_clock::now(); // 记录真实物理时间的结束时刻

    // 计算实际花费的真实物理时间
    auto wall_clock_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(wall_clock_end - wall_clock_start);

    std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): 实时调度器运行结束。" << std::endl;
    std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): 目标仿真时长: " << simulation_duration.count() * 1000 << " 毫秒。 "
              << "实际花费的真实物理时间: " << wall_clock_elapsed.count() << " 毫秒。" << std::endl;

    // 同样，对已分离任务的 is_done() 检查意义有限。
    // 关注协程内部的结束日志更为可靠。
    // std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): Sensor任务句柄状态: " << (sensor_task_rt.is_done() ? "已完成/分离" : "未完成") << std::endl;
    // std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): AVC任务句柄状态: " << (avc_task_rt.is_done() ? "已完成/分离" : "未完成") << std::endl;
    // std::cout << "[" << current_sim_ms_avc(rt_scheduler) << "毫秒] 主程序 (实时模式): LoadMonitor任务句柄状态: " << (load_monitor_task_rt.is_done() ? "已完成/分离" : "未完成") << std::endl;

    std::cout << "--- 实时模式AVC复杂场景仿真结束 ---" << std::endl;
}