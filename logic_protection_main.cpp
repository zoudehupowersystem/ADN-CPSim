#include "cps_coro_lib.h"
#include "ecs_core.h"
#include "logging_utils.h"
#include "logic_protection_system.h"

#include <chrono>
#include <iostream>

int main()
{
    initialize_loggers("logic_protection.log", true);
    std::cout << "--- 主动配电网CPS统一行为建模与高效仿真平台 ---\n";
    std::cout << "--- 场景: 保护与网络重构协同仿真 ---\n\n";

    cps_coro::Scheduler scheduler;
    Registry registry;

    LogicProtectionSystem protection_sim(registry, scheduler);
    protection_sim.initialize_scenario_entities();
    protection_sim.simulate_fault_and_reconfiguration_scenario().detach();

    scheduler.run_until(scheduler.now() + std::chrono::seconds(20));

    std::cout << "\n--- 仿真循环结束 ---\n";
    shutdown_loggers();
    return 0;
}