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

extern cps_coro::Scheduler* g_scheduler;
extern long get_peak_memory_usage_kb();
// 发电机任务 (Generator Task)
// 模拟发电机的启动过程和对功率调整请求的响应。
cps_coro::Task generatorTask()
{
    // 使用全局日志记录器 g_console_logger 和全局调度器 g_scheduler (确保它们已初始化)
    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [发电机] 启动序列已启动。", g_scheduler->now().time_since_epoch().count());

    co_await cps_coro::delay(cps_coro::Scheduler::duration(1000)); // 模拟发电机启动耗时1秒 (仿真时间)

    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [发电机] 已成功并网，运行稳定。", g_scheduler->now().time_since_epoch().count());

    if (g_scheduler)
        g_scheduler->trigger_event(GENERATOR_READY_EVENT); // 触发发电机就绪事件

    // 发电机进入稳定运行状态后，持续监听功率调整请求
    while (true) {
        // 等待功率调整请求事件 (POWER_ADJUST_REQUEST_EVENT)
        // 此处假设此事件不携带具体数据 (void 事件)
        co_await cps_coro::wait_for_event<void>(POWER_ADJUST_REQUEST_EVENT);

        if (g_console_logger && g_scheduler)
            g_console_logger->info("[{}毫秒] [发电机] 收到功率调整请求 (POWER_ADJUST_REQUEST_EVENT)。正在执行调整...", g_scheduler->now().time_since_epoch().count());

        co_await cps_coro::delay(cps_coro::Scheduler::duration(300)); // 模拟功率调整过程耗时300毫秒

        if (g_console_logger && g_scheduler)
            g_console_logger->info("[{}毫秒] [发电机] 功率输出已调整完毕。", g_scheduler->now().time_since_epoch().count());
    }
}

// 负荷任务 (Load Task)
// 模拟电力系统中负荷的变化行为。
cps_coro::Task loadTask()
{
    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [负荷] 正在等待发电机就绪 (GENERATOR_READY_EVENT) 事件...", g_scheduler->now().time_since_epoch().count());

    // 等待发电机就绪事件，然后才能施加初始负荷
    co_await cps_coro::wait_for_event<void>(GENERATOR_READY_EVENT);

    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [负荷] 检测到发电机已并网。正在施加初始负荷。", g_scheduler->now().time_since_epoch().count());

    co_await cps_coro::delay(cps_coro::Scheduler::duration(500)); // 模拟初始负荷稳定耗时500毫秒

    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [负荷] 负荷发生变化 (增加)。正在触发负荷变化事件 (LOAD_CHANGE_EVENT)。", g_scheduler->now().time_since_epoch().count());

    if (g_scheduler)
        g_scheduler->trigger_event(LOAD_CHANGE_EVENT); // 触发一个通用的负荷变化事件 (不带数据)

    // 模拟一段时间后负荷再次发生显著变化
    co_await cps_coro::delay(cps_coro::Scheduler::duration(10000)); // 等待10秒 (仿真时间)

    if (g_console_logger && g_scheduler)
        g_console_logger->info("[{}毫秒] [负荷] 负荷发生显著变化 (大幅增加)。正在触发负荷变化事件 (LOAD_CHANGE_EVENT) 及系统稳定性风险事件 (STABILITY_CONCERN_EVENT)。", g_scheduler->now().time_since_epoch().count());

    if (g_scheduler) {
        g_scheduler->trigger_event(LOAD_CHANGE_EVENT);
        g_scheduler->trigger_event(STABILITY_CONCERN_EVENT); // 同时触发一个稳定性风险事件
    }
    co_return; // 负荷任务的模拟序列结束
}

void test_vpp()
{

    // --- 创建调度器和ECS注册表实例 ---
    cps_coro::Scheduler scheduler_instance; // 创建标准事件调度器
    g_scheduler = &scheduler_instance; // 初始化全局调度器指针，使其指向此实例
    Registry registry; // 创建ECS注册表实例

    if (g_console_logger)
        g_console_logger->info("--- 主动配电网CPS统一行为建模与高效仿真平台 ---");
    if (g_console_logger)
        g_console_logger->info("日志系统: spdlog。仿真模式: 事件驱动VPP, 包含统计数据。");

    // 设置仿真初始时间 (通常为0)
    g_scheduler->set_time(cps_coro::Scheduler::time_point { std::chrono::milliseconds(0) });
    if (g_console_logger)
        g_console_logger->info("仿真初始时间已设置为: {} 毫秒。", g_scheduler->now().time_since_epoch().count());

    // --- 初始化继电保护系统模块 ---
    ProtectionSystem protection_system(registry, scheduler_instance); // 创建保护系统实例，传入注册表和调度器

    // 创建被保护设备实体，并为其添加保护组件
    Entity line1_prot = registry.create(); // 模拟一条被保护的线路
    registry.emplace<OverCurrentProtection>(line1_prot, 5.0, 200, "线路1过流保护-速动段"); // 电流定值5kA, 延时200ms
    registry.emplace<DistanceProtection>(line1_prot, 5.0, 0, 15.0, 300, 25.0, 700); // I段:5Ω,0ms; II段:15Ω,300ms; III段:25Ω,700ms

    Entity transformer1_prot = registry.create(); // 模拟一台被保护的变压器
    registry.emplace<OverCurrentProtection>(transformer1_prot, 2.5, 300, "变压器1过流保护-主保护段"); // 电流定值2.5kA, 延时300ms

    if (g_console_logger)
        g_console_logger->info("已创建保护实体: 线路1_保护 (实体ID #{}), 变压器1_保护 (实体ID #{})。", line1_prot, transformer1_prot);

    // 启动保护系统的核心运行任务和相关的辅助任务 (故障注入器、断路器代理)
    auto prot_sys_run_task = protection_system.run();
    prot_sys_run_task.detach(); // 分离任务，让其在调度器中独立运行

    // 注意：faultInjectorTask_prot 和 circuitBreakerAgentTask_prot 现在需要 scheduler_instance 作为参数
    auto fault_inject_prot_task = faultInjectorTask_prot(protection_system, line1_prot, transformer1_prot, scheduler_instance);
    fault_inject_prot_task.detach();
    auto breaker_l1p_task = circuitBreakerAgentTask_prot(line1_prot, "线路1_保护设备", scheduler_instance);
    breaker_l1p_task.detach();
    auto breaker_t1p_task = circuitBreakerAgentTask_prot(transformer1_prot, "变压器1_保护设备", scheduler_instance);
    breaker_t1p_task.detach();

    if (g_console_logger)
        g_console_logger->info("继电保护系统相关任务已启动。");

    // --- 初始化频率响应系统模块 (VPP) ---
    std::vector<Entity> ev_pile_entities_freq; // 存储所有EV充电桩实体的ID
    std::vector<Entity> ess_unit_entities_freq; // 存储所有ESS单元实体的ID
    std::random_device rd_freq; // 用于生成随机数种子
    std::mt19937 rng_freq(rd_freq()); // Mersenne Twister 随机数引擎
    std::uniform_real_distribution<double> soc_dist_freq(0.25, 0.90); // SOC在25%到90%之间均匀分布

    int num_ev_stations = 10; // 模拟的EV充电站数量
    int piles_per_station = 5; // 每个充电站的充电桩数量
    int total_ev_piles = num_ev_stations * piles_per_station; // 总EV充电桩数量

    // 创建并配置EV充电桩实体
    for (int i = 0; i < total_ev_piles; ++i) {
        Entity pile = registry.create();
        ev_pile_entities_freq.push_back(pile);
        double initial_soc = soc_dist_freq(rng_freq); // 随机生成初始SOC
        double scheduled_charging_power_kW; // 计划充电功率
        // 示例：按一定比例设置不同的计划充电功率
        if (i % 3 == 0)
            scheduled_charging_power_kW = -5.0; // 充电功率为5kW (负值表示消耗)
        else if (i % 3 == 1)
            scheduled_charging_power_kW = -3.5; // 充电功率为3.5kW
        else
            scheduled_charging_power_kW = 0.0; // 不充电 (或等待调度)

        // 为EV充电桩实体添加频率控制配置组件和物理状态组件
        registry.emplace<FrequencyControlConfigComponent>(pile,
            FrequencyControlConfigComponent::DeviceType::EV_PILE, // 类型
            scheduled_charging_power_kW, // 基准功率
            4.0, // 增益 (kW/Hz)
            0.03, // 死区 (Hz)
            5.0, // 最大输出功率 (放电, kW)
            -5.0, // 最小输出功率 (充电, kW)
            0.1, // SOC最小阈值 (10%)
            0.95 // SOC最大阈值 (95%)
        );
        registry.emplace<PhysicalStateComponent>(pile, scheduled_charging_power_kW, initial_soc);
    }
    if (g_console_logger)
        g_console_logger->info("已初始化 {} 个电动汽车充电桩用于频率响应仿真。", total_ev_piles);

    // 创建并配置ESS单元实体
    int num_ess_units = 100; // 模拟的ESS单元数量
    for (int i = 0; i < num_ess_units; ++i) {
        Entity ess = registry.create();
        ess_unit_entities_freq.push_back(ess);
        // 示例：ESS的增益设置，使其在频率偏差0.03Hz时能提供1000kW的响应
        double ess_gain_kw_per_hz = (1000.0) / (0.03 * 1.0); // 假设df=0.03Hz时，P_adj=1000kW，则K=P_adj/df (这里的系数50可能是额定频率，需要确认模型)
                                                             // 简化：若df=0.03Hz对应1000kW，则K = 1000/0.03
        ess_gain_kw_per_hz = 1000.0 / 0.03; // 修正增益计算

        registry.emplace<FrequencyControlConfigComponent>(ess,
            FrequencyControlConfigComponent::DeviceType::ESS_UNIT, // 类型
            0.0, // 基准功率 (假设平时待机)
            ess_gain_kw_per_hz, // 增益 (kW/Hz)
            0.03, // 死区 (Hz)
            1000.0, // 最大输出功率 (放电, kW)
            -1000.0, // 最小输出功率 (充电, kW)
            0.05, // SOC最小阈值 (5%)
            0.95 // SOC最大阈值 (95%)
        );
        registry.emplace<PhysicalStateComponent>(ess, 0.0, 0.7); // 初始功率0, 初始SOC 70%
    }
    if (g_console_logger)
        g_console_logger->info("已初始化 {} 个储能单元 (ESS) 用于频率响应仿真。", num_ess_units);

    // 启动频率响应系统的核心任务 (频率预言机、EV VPP控制器、ESS VPP控制器)
    double freq_sim_step_ms = 20.0; // 频率预言机的更新步长设为20毫秒
    auto freq_oracle_task_main = frequencyOracleTask(registry, ev_pile_entities_freq, ess_unit_entities_freq, 5.0, freq_sim_step_ms);
    // 参数：registry, ev实体, ess实体, 扰动开始时间(仿真5秒时), 频率更新步长(ms)
    freq_oracle_task_main.detach();

    auto ev_vpp_task_main = vppFrequencyResponseTask(registry, "电动汽车VPP", ev_pile_entities_freq, freq_sim_step_ms);
    ev_vpp_task_main.detach();
    auto ess_vpp_task_main = vppFrequencyResponseTask(registry, "储能系统VPP", ess_unit_entities_freq, freq_sim_step_ms);
    ess_vpp_task_main.detach();

    if (g_console_logger)
        g_console_logger->info("频率-有功功率响应系统 (VPP) 相关任务已启动。");

    // --- 启动通用后台任务 (发电机、负荷) ---
    auto gen_task_main = generatorTask();
    gen_task_main.detach();
    auto load_task_main = loadTask();
    load_task_main.detach();
    if (g_console_logger)
        g_console_logger->info("通用后台仿真任务 (发电机、负荷等) 已启动。");

    // --- 运行仿真 ---
    auto real_time_sim_start = std::chrono::high_resolution_clock::now(); // 记录仿真开始时的物理时钟时间

    // 设定总仿真时长
    cps_coro::Scheduler::duration simulation_duration = std::chrono::milliseconds(70000); // 模拟70秒的仿真时间
    cps_coro::Scheduler::time_point end_time = g_scheduler->now() + simulation_duration; // 计算仿真结束的绝对时间点

    if (g_console_logger)
        g_console_logger->info("\n--- 即将开始运行主仿真循环，直至仿真时间到达 {} 毫秒 --- \n", end_time.time_since_epoch().count());

    // 执行仿真循环
    g_scheduler->run_until(end_time);

    auto real_time_sim_end = std::chrono::high_resolution_clock::now(); // 记录仿真结束时的物理时钟时间
    std::chrono::duration<double> real_time_elapsed_seconds = real_time_sim_end - real_time_sim_start; // 计算总物理耗时

    // --- 仿真结束后的统计和清理 ---
    if (g_console_logger) {
        g_console_logger->info("\n--- 主仿真循环已结束 --- ");
        g_console_logger->info("最终仿真时间: {} 毫秒。", g_scheduler->now().time_since_epoch().count());
        g_console_logger->info("仿真实际物理执行耗时: {:.3f} 秒。", real_time_elapsed_seconds.count());
    }

    // 获取并打印峰值内存使用情况
    long peak_mem_kb = get_peak_memory_usage_kb();
    if (peak_mem_kb != -1 && g_console_logger) {
        g_console_logger->info("本次仿真峰值内存使用 (近似值): {} KB (约 {:.2f} MB)。", peak_mem_kb, peak_mem_kb / 1024.0);
    } else if (g_console_logger) {
        g_console_logger->warn("未能成功获取本次仿真的峰值内存使用数据 (可能当前平台不支持或获取失败)。");
    }

    if (g_console_logger)
        g_console_logger->info("VPP频率响应仿真数据已保存至配置文件中指定的路径 ({}).", "虚拟电厂频率响应数据.csv");
}