// logic_protection_system.cpp
#include "logic_protection_system.h"
#include <string>
#include <unordered_set>

LogicProtectionSystem::LogicProtectionSystem(Registry& registry, cps_coro::Scheduler& scheduler)
    : registry_(registry)
    , scheduler_(scheduler)
{
}

void LogicProtectionSystem::initialize_scenario_entities()
{
    log_lp_info(scheduler_, "==> 1. 开始初始化保护与网络重构协同仿真场景...");

    bus_entities["1M"] = registry_.create();
    registry_.emplace<BusIdentityComponent>(bus_entities["1M"], "母线1M(电源A)", true);
    bus_entities["2M"] = registry_.create();
    registry_.emplace<BusIdentityComponent>(bus_entities["2M"], "母线2M");
    bus_entities["3M"] = registry_.create();
    registry_.emplace<BusIdentityComponent>(bus_entities["3M"], "母线3M");
    bus_entities["4M"] = registry_.create();
    registry_.emplace<BusIdentityComponent>(bus_entities["4M"], "母线4M");
    bus_entities["5M"] = registry_.create();
    registry_.emplace<BusIdentityComponent>(bus_entities["5M"], "母线5M(电源E)", true);
    line_entities["L1"] = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_entities["L1"], "线路L1", bus_entities["1M"], bus_entities["2M"]);
    line_entities["L2"] = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_entities["L2"], "线路L2", bus_entities["2M"], bus_entities["3M"]);
    line_entities["L3"] = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_entities["L3"], "线路L3", bus_entities["3M"], bus_entities["4M"]);
    line_entities["L4"] = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_entities["L4"], "线路L4", bus_entities["4M"], bus_entities["5M"]);

    //  创建断路器实体, 包含精确的母线连接点 ---
    breaker_entities["1DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["1DL"], "断路器1DL", line_entities["L1"], bus_entities["1M"]);
    breaker_entities["2DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["2DL"], "断路器2DL", line_entities["L1"], bus_entities["2M"]);
    breaker_entities["3DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["3DL"], "断路器3DL", line_entities["L2"], bus_entities["2M"], true); // 3DL拒动
    breaker_entities["4DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["4DL"], "断路器4DL", line_entities["L2"], bus_entities["3M"]);
    breaker_entities["5DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["5DL"], "断路器5DL", line_entities["L3"], bus_entities["3M"]);
    breaker_entities["6DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["6DL"], "断路器6DL(联络)", line_entities["L3"], bus_entities["4M"]);
    breaker_entities["7DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["7DL"], "断路器7DL", line_entities["L4"], bus_entities["4M"]);
    breaker_entities["8DL"] = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_entities["8DL"], "断路器8DL", line_entities["L4"], bus_entities["5M"]);
    for (const auto& pair : breaker_entities) {
        bool is_normally_open = (pair.first == "6DL");
        registry_.emplace<BreakerStateComponent>(pair.second, is_normally_open, is_normally_open);
    }
    log_lp_info(scheduler_, "场景实体和状态创建完成. 6DL为常开点, 3DL为拒动断路器.");
    std::vector<BusId> all_buses;
    for (const auto& pair : bus_entities)
        all_buses.push_back(pair.second);
    std::vector<BranchId> all_lines;
    std::vector<std::pair<BusId, BusId>> all_line_endpoints;
    for (const auto& pair : line_entities) {
        auto line_comp = registry_.get<LineIdentityComponent>(pair.second);
        all_lines.push_back(pair.second);
        all_line_endpoints.push_back({ line_comp->from_bus_entity, line_comp->to_bus_entity });
    }
    topology_.buildTopology(all_buses, all_lines, all_line_endpoints);
    log_lp_info(scheduler_, "拓扑服务构建完成. 模型: 母线=节点, 线路=支路.");

    protection_entities["Prot_L2_Main"] = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(protection_entities["Prot_L2_Main"], "L2主保护", ProtectionDeviceComponent::Type::MAIN,
        std::vector<Entity> { line_entities["L2"] }, std::vector<Entity> { breaker_entities["3DL"], breaker_entities["4DL"] }, 50);
    protection_entities["Prot_L1_Backup"] = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(protection_entities["Prot_L1_Backup"], "L1后备保护(带方向)", ProtectionDeviceComponent::Type::BACKUP,
        std::vector<Entity> { line_entities["L1"] }, std::vector<Entity> { breaker_entities["1DL"] }, 1000,
        std::vector<Entity> { line_entities["L2"] });
    protection_entities["Prot_L3_Backup"] = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(protection_entities["Prot_L3_Backup"], "L3后备保护(带方向)", ProtectionDeviceComponent::Type::BACKUP,
        std::vector<Entity> { line_entities["L3"] }, std::vector<Entity> { breaker_entities["5DL"] }, 1500,
        std::vector<Entity> { line_entities["L2"] });
    log_lp_info(scheduler_, "保护装置配置完成 (已模拟方向性并使用真实延时).");

    reconfig_system_entity = registry_.create();
    for (const auto& pair : breaker_entities)
        breaker_logic_task(pair.second).detach();
    for (const auto& pair : protection_entities)
        protection_device_logic_task(pair.second).detach();
    network_reconfiguration_logic_task().detach();
    log_lp_info(scheduler_, "为所有非电源母线启动失电监视任务...");
    for (const auto& pair : bus_entities) {
        auto bus_comp = registry_.get<BusIdentityComponent>(pair.second);
        if (bus_comp && !bus_comp->is_power_source) {
            supply_check_task(pair.second).detach();
            log_lp_info(scheduler_, "  -> 已启动对母线 [%s] 的监视.", bus_comp->name.c_str());
        }
    }
    log_lp_info(scheduler_, "==> 所有协程任务已启动. 初始化完成. <==");
}

cps_coro::Task LogicProtectionSystem::network_reconfiguration_logic_task()
{
    log_lp_info(scheduler_, "网络重构系统任务启动, 等待任意母线失电事件...");
    while (true) {
        LogicSupplyLossInfo loss_info = co_await cps_coro::wait_for_event<LogicSupplyLossInfo>(to_underlying(EventID::LOGIC_SUPPLY_LOSS_EVENT));
        auto lost_bus_comp = registry_.get<BusIdentityComponent>(loss_info.bus_entity);
        if (!lost_bus_comp)
            continue;

        log_lp_info(scheduler_, "网络重构: 检测到母线 [%s] 失电. 将在10秒后启动决策...", lost_bus_comp->name.c_str());
        co_await cps_coro::delay(std::chrono::seconds(10));

        if (is_bus_connected_to_source(loss_info.bus_entity)) {
            log_lp_info(scheduler_, "网络重构: 母线 [%s] 在等待期间已恢复供电, 取消本次重构.", lost_bus_comp->name.c_str());
            continue;
        }

        log_lp_info(scheduler_, "网络重构: 10秒延时结束, 母线 [%s] 仍失电. 启动动态决策引擎.", lost_bus_comp->name.c_str());

        auto option = find_reconfiguration_option(loss_info.bus_entity, active_fault_line_);

        if (option) {
            auto breaker_to_close_name = registry_.get<BreakerIdentityComponent>(option->breaker_to_close)->name;
            log_lp_info(scheduler_, "网络重构决策完成: 最优方案是合上断路器 [%s].", breaker_to_close_name.c_str());
            scheduler_.trigger_event(to_underlying(EventID::LOGIC_BREAKER_COMMAND_EVENT), LogicBreakerCommand { option->breaker_to_close, LogicBreakerCommand::CommandType::CLOSE });

            co_await cps_coro::delay(std::chrono::milliseconds(200));

            if (is_bus_connected_to_source(loss_info.bus_entity)) {
                log_lp_info(scheduler_, "网络重构: 成功恢复了对母线 [%s] 的供电!", lost_bus_comp->name.c_str());
            } else {
                log_lp_info(scheduler_, "网络重构: 执行合闸后, 母线 [%s] 仍失电, 重构失败.", lost_bus_comp->name.c_str());
            }
        } else {
            log_lp_info(scheduler_, "网络重构决策完成: 未找到可行的恢复方案来恢复母线 [%s].", lost_bus_comp->name.c_str());
        }
    }
}

cps_coro::Task LogicProtectionSystem::simulate_fault_and_reconfiguration_scenario()
{
    log_lp_info(scheduler_, "--- 开始保护与网络重构协同仿真 (V5) ---");
    co_await cps_coro::delay(std::chrono::milliseconds(100));

    log_lp_info(scheduler_, "### 故障注入: 在线路 [L2] 注入永久性故障. ###");
    active_fault_line_ = line_entities["L2"]; // 记录活动故障
    scheduler_.trigger_event(to_underlying(EventID::LOGIC_FAULT_EVENT), LogicFaultInfo { active_fault_line_ });

    co_await cps_coro::delay(std::chrono::seconds(15));

    log_lp_info(scheduler_, "--- 仿真结束, 验证最终状态 ---");
    auto get_state_str = [&](const std::string& name) {
        auto state_comp = registry_.get<BreakerStateComponent>(breaker_entities.at(name));
        return state_comp->is_open ? std::string("打开") : std::string("闭合");
    };

    log_lp_info(scheduler_, "最终状态: 1DL(%s), 2DL(%s), 3DL(%s), 4DL(%s), 5DL(%s), 6DL(%s)",
        get_state_str("1DL").c_str(), get_state_str("2DL").c_str(), get_state_str("3DL").c_str(),
        get_state_str("4DL").c_str(), get_state_str("5DL").c_str(), get_state_str("6DL").c_str());

    bool success = get_state_str("1DL") == "打开" && get_state_str("2DL") == "闭合" && get_state_str("3DL") == "闭合" && get_state_str("4DL") == "打开" && get_state_str("5DL") == "闭合" && get_state_str("6DL") == "闭合";

    if (success) {
        log_lp_info(scheduler_, "+++ 验证成功: 保护与重构序列完全符合预期! +++");
    } else {
        log_lp_info(scheduler_, "--- 验证失败: 最终状态不符合预期. ---");
    }
    co_return;
}

std::optional<ReconfigurationOption> LogicProtectionSystem::find_reconfiguration_option(Entity lost_bus_entity, Entity faulted_line)
{
    auto lost_bus_name = registry_.get<BusIdentityComponent>(lost_bus_entity)->name;

    // 通用安全前置条件检查
    log_lp_info(scheduler_, "决策分析: 对母线 [%s] 进行安全前置条件检查...", lost_bus_name.c_str());
    bool is_safe_to_reconfigure = true;
    registry_.for_each<BreakerIdentityComponent>([&](BreakerIdentityComponent& breaker_id, Entity breaker_entity) {
        if (!is_safe_to_reconfigure)
            return;

        // 检查此断路器是否物理连接在失电母线上
        if (breaker_id.connected_bus_entity == lost_bus_entity) {
            // 如果此断路器关联的线路是故障线路
            if (breaker_id.associated_line_entity == faulted_line) {
                auto breaker_state = registry_.get<BreakerStateComponent>(breaker_entity);
                // 那么此断路器必须是断开的
                if (breaker_state && !breaker_state->is_open) {
                    log_lp_info(scheduler_, "决策分析失败: 母线 [%s] 通过闭合的断路器 [%s] 直接连接到了故障线路. 禁止重构!",
                        lost_bus_name.c_str(), breaker_id.name.c_str());
                    is_safe_to_reconfigure = false;
                }
            }
        }
    });

    if (!is_safe_to_reconfigure) {
        return std::nullopt;
    }
    log_lp_info(scheduler_, "决策分析: 安全检查通过. 母线 [%s] 已与故障隔离.", lost_bus_name.c_str());

    // 2. 搜索最佳恢复路径
    ReconfigurationOption best_option;
    best_option.path_length = std::numeric_limits<int>::max(); // 重置
    log_lp_info(scheduler_, "决策分析: 开始搜索最佳恢复路径...");
    registry_.for_each<BreakerStateComponent>([&](BreakerStateComponent& state, Entity breaker_entity) {
        if (!state.is_normally_open)
            return;

        auto breaker_id = registry_.get<BreakerIdentityComponent>(breaker_entity);
        if (!breaker_id)
            return;

        auto line_on_breaker = registry_.get<LineIdentityComponent>(breaker_id->associated_line_entity);
        if (!line_on_breaker)
            return;

        log_lp_info(scheduler_, "  -> 正在评估候选开关 [%s]...", breaker_id->name.c_str());

        Entity endpoint1 = line_on_breaker->from_bus_entity;
        Entity endpoint2 = line_on_breaker->to_bus_entity;

        auto evaluate_path = [&](Entity source_side, Entity load_side) -> std::optional<int> {
            if (is_bus_connected_to_source(source_side)) {
                auto open_lines = get_currently_open_lines();
                auto it = std::remove(open_lines.begin(), open_lines.end(), breaker_id->associated_line_entity);
                open_lines.erase(it, open_lines.end());

                auto path = topology_.findPath(source_side, lost_bus_entity, open_lines);
                if (path && !path->buses.empty()) {
                    log_lp_info(scheduler_, "    - 候选开关 [%s] 可行: 可从带电母线 [%s] 经拓扑距离 %zu 到达失电母线.",
                        breaker_id->name.c_str(), registry_.get<BusIdentityComponent>(source_side)->name.c_str(), path->buses.size());
                    return static_cast<int>(path->buses.size());
                }
            }
            return std::nullopt;
        };

        if (auto len = evaluate_path(endpoint1, lost_bus_entity)) {
            if (*len < best_option.path_length) {
                best_option.breaker_to_close = breaker_entity;
                best_option.path_length = *len;
            }
        }
        if (auto len = evaluate_path(endpoint2, lost_bus_entity)) {
            if (*len < best_option.path_length) {
                best_option.breaker_to_close = breaker_entity;
                best_option.path_length = *len;
            }
        }
    });

    if (best_option.breaker_to_close != 0) {
        return best_option;
    }
    return std::nullopt;
}

cps_coro::Task LogicProtectionSystem::protection_device_logic_task(Entity p_entity)
{
    auto prot_comp = registry_.get<ProtectionDeviceComponent>(p_entity);
    if (!prot_comp)
        co_return;

    while (true) {
        LogicFaultInfo fault_info = co_await cps_coro::wait_for_event<LogicFaultInfo>(to_underlying(EventID::LOGIC_FAULT_EVENT));

        bool is_relevant = false;
        if (prot_comp->type == ProtectionDeviceComponent::Type::MAIN) {
            for (auto p_line : prot_comp->protected_entities) {
                if (p_line == fault_info.faulted_line_entity) {
                    is_relevant = true;
                    break;
                }
            }
        } else { // BACKUP
            for (auto b_line : prot_comp->backup_protected_entities) {
                if (b_line == fault_info.faulted_line_entity) {
                    is_relevant = true;
                    break;
                }
            }
        }

        if (is_relevant) {
            log_lp_info(scheduler_, "保护 [%s] 检测到相关故障, 启动计时 (延时: %lldms).", prot_comp->name.c_str(), prot_comp->trip_delay.count());
            co_await cps_coro::delay(prot_comp->trip_delay);

            if (is_line_energized(fault_info.faulted_line_entity)) {
                log_lp_info(scheduler_, "保护 [%s] 计时结束, 故障仍存在, 发出跳闸命令!", prot_comp->name.c_str());
                for (auto breaker : prot_comp->commanded_breaker_entities) {
                    scheduler_.trigger_event(to_underlying(EventID::LOGIC_BREAKER_COMMAND_EVENT), LogicBreakerCommand { breaker, LogicBreakerCommand::CommandType::OPEN });
                }
            } else {
                log_lp_info(scheduler_, "保护 [%s] 计时结束, 故障已被其他保护清除, 复归.", prot_comp->name.c_str());
            }
        }
    }
}

cps_coro::Task LogicProtectionSystem::breaker_logic_task(Entity breaker_entity)
{
    auto id_comp = registry_.get<BreakerIdentityComponent>(breaker_entity);
    auto state_comp = registry_.get<BreakerStateComponent>(breaker_entity);
    if (!id_comp || !state_comp)
        co_return;

    while (true) {
        LogicBreakerCommand cmd = co_await cps_coro::wait_for_event<LogicBreakerCommand>(to_underlying(EventID::LOGIC_BREAKER_COMMAND_EVENT));
        if (cmd.breaker_entity != breaker_entity)
            continue;

        if (cmd.command == LogicBreakerCommand::CommandType::OPEN) {
            if (!state_comp->is_open) {
                if (id_comp->is_stuck_on_trip_cmd) {
                    log_lp_info(scheduler_, "!!! 断路器 [%s] 发生拒动! 保持闭合状态.", id_comp->name.c_str());
                } else {
                    log_lp_info(scheduler_, "断路器 [%s] 收到跳闸命令, 正在动作...", id_comp->name.c_str());
                    co_await cps_coro::delay(std::chrono::milliseconds(20));
                    state_comp->is_open = true;
                    log_lp_info(scheduler_, ">>> 断路器 [%s] 已成功打开.", id_comp->name.c_str());
                    scheduler_.trigger_event(to_underlying(EventID::LOGIC_BREAKER_STATUS_CHANGED_EVENT), LogicBreakerStatus { breaker_entity, true });
                }
            }
        } else { // CLOSE
            if (state_comp->is_open) {
                log_lp_info(scheduler_, "断路器 [%s] 收到合闸命令, 正在动作...", id_comp->name.c_str());
                co_await cps_coro::delay(std::chrono::milliseconds(100));
                state_comp->is_open = false;
                log_lp_info(scheduler_, ">>> 断路器 [%s] 已成功闭合.", id_comp->name.c_str());
                scheduler_.trigger_event(to_underlying(EventID::LOGIC_BREAKER_STATUS_CHANGED_EVENT), LogicBreakerStatus { breaker_entity, false });
            }
        }
    }
}

cps_coro::Task LogicProtectionSystem::supply_check_task(Entity bus_entity)
{
    auto bus_id_comp = registry_.get<BusIdentityComponent>(bus_entity);
    if (!bus_id_comp)
        co_return;

    bool was_energized = true;
    while (true) {
        co_await cps_coro::wait_for_event<LogicBreakerStatus>(to_underlying(EventID::LOGIC_BREAKER_STATUS_CHANGED_EVENT));
        co_await cps_coro::delay(std::chrono::milliseconds(10));

        bool is_energized = is_bus_connected_to_source(bus_entity);

        if (was_energized && !is_energized) {
            log_lp_info(scheduler_, "!!! 监视器: 检测到母线 [%s] 已失电!", bus_id_comp->name.c_str());
            scheduler_.trigger_event(to_underlying(EventID::LOGIC_SUPPLY_LOSS_EVENT), LogicSupplyLossInfo { bus_entity });
        }
        was_energized = is_energized;
    }
}

std::vector<BranchId> LogicProtectionSystem::get_currently_open_lines()
{
    std::unordered_set<Entity> open_lines_set;
    registry_.for_each<BreakerStateComponent>([&](BreakerStateComponent& state, Entity b_entity) {
        if (state.is_open) {
            auto id_comp = registry_.get<BreakerIdentityComponent>(b_entity);
            if (id_comp) {
                open_lines_set.insert(id_comp->associated_line_entity);
            }
        }
    });
    return { open_lines_set.begin(), open_lines_set.end() };
}

bool LogicProtectionSystem::is_bus_connected_to_source(BusId target_bus)
{
    auto open_lines = get_currently_open_lines();
    bool connected_to_A = topology_.findPath(bus_entities["1M"], target_bus, open_lines).has_value();
    bool connected_to_E = topology_.findPath(bus_entities["5M"], target_bus, open_lines).has_value();
    return connected_to_A || connected_to_E;
}

bool LogicProtectionSystem::is_line_energized(Entity line_entity)
{
    auto line_id_comp = registry_.get<LineIdentityComponent>(line_entity);
    if (!line_id_comp)
        return false;

    bool from_bus_energized = is_bus_connected_to_source(line_id_comp->from_bus_entity);
    bool to_bus_energized = is_bus_connected_to_source(line_id_comp->to_bus_entity);

    return from_bus_energized || to_bus_energized;
}