// logic_protection_system.cpp
#include "logic_protection_system.h"

// LogicProtectionSystem 类构造函数
LogicProtectionSystem::LogicProtectionSystem(Registry& registry, cps_coro::Scheduler& scheduler)
    : registry_(registry)
    , scheduler_(scheduler)
{
    if (g_console_logger) {
        char buffer[200];
        snprintf(buffer, sizeof(buffer), "[LogicProtectionSystem::Ctor] this: %p, registry_ ref addr: %p, scheduler_ ref addr: %p",
            (void*)this, (void*)&registry_, (void*)&scheduler_);
        g_console_logger->debug(buffer);
    }
}

// 初始化场景实体 (线路、断路器、保护装置)
void LogicProtectionSystem::initialize_scenario_entities()
{
    log_lp_info(scheduler_, "正在初始化逻辑保护场景: 3条线路 (A-B-C), 主/后备保护, 断路器.");

    // --- 1. 创建线路实体及其关联的断路器实体 ---
    // 拓扑结构: 电源 -> A -> B -> C (C为最下游)

    // --- C 线路 和 C 断路器 (最下游) ---
    breaker_C_entity = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_C_entity, "断路器C", Entity {}, true);
    registry_.emplace<BreakerStateComponent>(breaker_C_entity);

    line_C_entity = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_C_entity, "线路C", breaker_C_entity,
        std::nullopt, std::nullopt);
    // 更新断路器C，指明它隔离的是线路C
    if (auto* comp = registry_.get<BreakerIdentityComponent>(breaker_C_entity)) {
        comp->line_entity_it_isolates = line_C_entity;
    } else {
        log_lp_info(scheduler_, "错误: 初始化时未能获取断路器C的BreakerIdentityComponent.");
    }

    // --- B 线路 和 B 断路器 ---
    breaker_B_entity = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_B_entity, "断路器B", Entity {}, false);
    registry_.emplace<BreakerStateComponent>(breaker_B_entity);

    line_B_entity = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_B_entity, "线路B", breaker_B_entity,
        std::nullopt, line_C_entity);
    if (auto* comp = registry_.get<BreakerIdentityComponent>(breaker_B_entity)) {
        comp->line_entity_it_isolates = line_B_entity;
    } else {
        log_lp_info(scheduler_, "错误: 初始化时未能获取断路器B的BreakerIdentityComponent.");
    }
    if (auto* comp = registry_.get<LineIdentityComponent>(line_C_entity)) {
        comp->upstream_line_entity = line_B_entity;
    } else {
        log_lp_info(scheduler_, "错误: 初始化时未能获取线路C的LineIdentityComponent (更新上游).");
    }

    // --- A 线路 和 A 断路器 (最上游) ---
    breaker_A_entity = registry_.create();
    registry_.emplace<BreakerIdentityComponent>(breaker_A_entity, "断路器A", Entity {}, false);
    registry_.emplace<BreakerStateComponent>(breaker_A_entity);

    line_A_entity = registry_.create();
    registry_.emplace<LineIdentityComponent>(line_A_entity, "线路A", breaker_A_entity,
        std::nullopt, line_B_entity);
    if (auto* comp = registry_.get<BreakerIdentityComponent>(breaker_A_entity)) {
        comp->line_entity_it_isolates = line_A_entity;
    } else {
        log_lp_info(scheduler_, "错误: 初始化时未能获取断路器A的BreakerIdentityComponent.");
    }
    if (auto* comp = registry_.get<LineIdentityComponent>(line_B_entity)) {
        comp->upstream_line_entity = line_A_entity;
    } else {
        log_lp_info(scheduler_, "错误: 初始化时未能获取线路B的LineIdentityComponent (更新上游).");
    }

    log_lp_info(scheduler_, "线路和断路器已创建: A (ID: %llu), B (ID: %llu), C (ID: %llu). "
                            "断路器: A_CB (ID: %llu), B_CB (ID: %llu), C_CB (ID: %llu, 拒动)",
        (unsigned long long)line_A_entity, (unsigned long long)line_B_entity, (unsigned long long)line_C_entity,
        (unsigned long long)breaker_A_entity, (unsigned long long)breaker_B_entity, (unsigned long long)breaker_C_entity);

    // --- 2. 创建保护装置实体 ---
    prot_main_C_entity = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(prot_main_C_entity, "C线路主保护",
        ProtectionDeviceComponent::Type::MAIN,
        line_C_entity, breaker_C_entity, 50);

    prot_main_B_entity = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(prot_main_B_entity, "B线路主保护",
        ProtectionDeviceComponent::Type::MAIN,
        line_B_entity, breaker_B_entity, 50);
    prot_backup_B_entity = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(prot_backup_B_entity, "B线路后备保护",
        ProtectionDeviceComponent::Type::BACKUP,
        line_B_entity, breaker_B_entity, 2000);

    prot_main_A_entity = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(prot_main_A_entity, "A线路主保护",
        ProtectionDeviceComponent::Type::MAIN,
        line_A_entity, breaker_A_entity, 50);
    prot_backup_A_entity = registry_.create();
    registry_.emplace<ProtectionDeviceComponent>(prot_backup_A_entity, "A线路后备保护",
        ProtectionDeviceComponent::Type::BACKUP,
        line_A_entity, breaker_A_entity, 3000);
    log_lp_info(scheduler_, "保护装置已创建.");

    // --- 3. 启动各个断路器和保护装置的独立逻辑协程任务 ---
    std::vector<Entity> all_breakers = { breaker_A_entity, breaker_B_entity, breaker_C_entity };
    for (Entity b_entity : all_breakers) {
        breaker_logic_task(b_entity).detach();
    }

    std::vector<Entity> all_protections = {
        prot_main_A_entity, prot_backup_A_entity,
        prot_main_B_entity, prot_backup_B_entity,
        prot_main_C_entity
    };
    for (Entity p_entity : all_protections) {
        protection_device_logic_task(p_entity).detach();
    }
    log_lp_info(scheduler_, "断路器和保护装置的逻辑协程任务已启动并分离.");
}

// 拓扑检查辅助函数
bool LogicProtectionSystem::is_line_downstream_or_same(Entity current_line_entity, Entity target_line_entity)
{
    LineIdentityComponent* initial_comp = nullptr;
    try {
        initial_comp = registry_.get<LineIdentityComponent>(current_line_entity);
    } catch (const std::exception& e) {
        log_lp_info(scheduler_, "拓扑辅助检查: 'is_line_downstream_or_same' - 异常于 registry_.get<LineIdentityComponent>(%llu): %s. 拓扑检查失败.",
            (unsigned long long)current_line_entity, e.what());
        return false; // Critical error if get itself throws
    }

    if (!initial_comp) {
        log_lp_info(scheduler_, "拓扑辅助检查: 'is_line_downstream_or_same' - 错误! 未能获取线路 %llu 的 LineIdentityComponent. 拓扑检查失败.", (unsigned long long)current_line_entity);
        return false;
    }

    std::string initial_line_name_str = "未知线路(名称获取失败)";
    const char* initial_line_name_cstr = initial_line_name_str.c_str();

    try {
        initial_line_name_str = initial_comp->name;
        initial_line_name_cstr = initial_line_name_str.c_str();
        if (g_console_logger) {
            g_console_logger->debug("[LP-Sim @ %lldms] 拓扑辅助检查: 'is_line_downstream_or_same' - 成功获取线路 %llu (%s) 的组件.",
                (long long)scheduler_.now().time_since_epoch().count(),
                (unsigned long long)current_line_entity, initial_line_name_cstr);
        }
    } catch (const std::exception& e) {
        if (g_console_logger) {
            g_console_logger->error("[LP-Sim @ %lldms] 拓扑辅助检查: 'is_line_downstream_or_same' - 尝试访问线路 %llu 的名称时发生C++异常: %s",
                (long long)scheduler_.now().time_since_epoch().count(),
                (unsigned long long)current_line_entity, e.what());
        }
    }

    Entity path_tracer_entity = current_line_entity;
    int safety_count = 0; // Max 10 iterations to prevent infinite loops in malformed topologies

    while (safety_count < 10) {

        log_lp_info(scheduler_, "拓扑辅助检查1: 'is_line_downstream_or_same' - 迭代 #%d, 当前追踪点: 线路 %llu, 目标线路: %llu",
            safety_count, (unsigned long long)path_tracer_entity, (unsigned long long)target_line_entity);

        if (path_tracer_entity == target_line_entity) {
            log_lp_info(scheduler_, "拓扑辅助检查2: 'is_line_downstream_or_same' - 找到匹配! 目标线路 %llu 是线路 %llu (%s) 的下游或本身. 结论: 是.",
                (unsigned long long)target_line_entity, (unsigned long long)current_line_entity, initial_line_name_cstr);
            return true;
        }

        LineIdentityComponent* line_id_comp = registry_.get<LineIdentityComponent>(path_tracer_entity);
        if (!line_id_comp) {
            log_lp_info(scheduler_, "拓扑辅助检查3: 'is_line_downstream_or_same' - 错误! 追踪点线路 %llu 没有 LineIdentityComponent. 终止路径追踪.",
                (unsigned long long)path_tracer_entity);
            return false; // Path broken
        }

        std::string tracer_name_str = "未知追踪点(名称获取失败)";
        const char* tracer_name_cstr_local = tracer_name_str.c_str();
        try {
            tracer_name_str = line_id_comp->name;
            tracer_name_cstr_local = tracer_name_str.c_str();
        } catch (const std::exception& e) {
            log_lp_info(scheduler_, "拓扑辅助检查4: 'is_line_downstream_or_same' - 尝试访问追踪点线路 %llu 的名称时发生C++异常: %s", (unsigned long long)path_tracer_entity, e.what());
        }

        if (!line_id_comp->downstream_line_entity) {
            log_lp_info(scheduler_, "拓扑辅助检查5: 'is_line_downstream_or_same' - 线路 %llu (%s) 已到达路径末端 (无下游线路), 未找到目标 %llu. 结论: 不是.",
                (unsigned long long)path_tracer_entity, tracer_name_cstr_local, (unsigned long long)target_line_entity);
            return false;
        }

        Entity previous_tracer_entity_id = path_tracer_entity; // For logging
        path_tracer_entity = line_id_comp->downstream_line_entity.value();

        log_lp_info(scheduler_, "拓扑辅助检查6: 'is_line_downstream_or_same' - 从线路 %s (ID: %llu) 追踪到其下游线路 (ID: %llu).",
            tracer_name_cstr_local, (unsigned long long)previous_tracer_entity_id, (unsigned long long)path_tracer_entity);
        safety_count++;
    }

    log_lp_info(scheduler_, "警告: 'is_line_downstream_or_same' 超出最大安全追踪次数 (%d次迭代) (从 %llu (%s) 到 %llu). 可能拓扑配置错误或路径过长. 假设不是下游.",
        safety_count, (unsigned long long)current_line_entity, initial_line_name_cstr, (unsigned long long)target_line_entity);
    return false;
}

// 拓扑检查核心函数
bool LogicProtectionSystem::is_fault_path_energized(Entity perspective_line_entity, Entity fault_on_line_entity)
{
    // Helper lambda to safely get entity name
    auto get_safe_entity_name = [&](Entity entity_id, const char* component_type_name_log, const char* default_name_prefix) -> std::string {
        std::string entity_name_str = std::string(default_name_prefix) + " (组件缺失或名称访问异常)";
        if (entity_id == 0)
            return std::string(default_name_prefix) + " (ID 0)";

        IComponent* comp_ptr = nullptr;
        std::string temp_name;

        if (std::string(component_type_name_log) == "LineIdentityComponent") {
            comp_ptr = registry_.get<LineIdentityComponent>(entity_id);
            if (comp_ptr) {
                try {
                    temp_name = static_cast<LineIdentityComponent*>(comp_ptr)->name;
                    entity_name_str = temp_name;
                } catch (const std::exception&) { /* entity_name_str remains default */
                }
            }
        } else if (std::string(component_type_name_log) == "BreakerIdentityComponent") {
            comp_ptr = registry_.get<BreakerIdentityComponent>(entity_id);
            if (comp_ptr) {
                try {
                    temp_name = static_cast<BreakerIdentityComponent*>(comp_ptr)->name;
                    entity_name_str = temp_name;
                } catch (const std::exception&) { /* entity_name_str remains default */
                }
            }
        }
        return entity_name_str;
    };

    std::string perspective_line_name_str = get_safe_entity_name(perspective_line_entity, "LineIdentityComponent", "视角线路");
    std::string fault_line_name_str = get_safe_entity_name(fault_on_line_entity, "LineIdentityComponent", "故障线路");

    log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 从线路 %llu (%s) 的视角, 判断到故障线路 %llu (%s) 的路径是否带电?",
        (unsigned long long)perspective_line_entity, perspective_line_name_str.c_str(),
        (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());

    Entity current_eval_line = perspective_line_entity;
    int safety_count = 0;

    while (current_eval_line != fault_on_line_entity && safety_count++ < 10) {
        std::string current_eval_line_name_str = get_safe_entity_name(current_eval_line, "LineIdentityComponent", "当前评估线路");
        auto line_id_comp = registry_.get<LineIdentityComponent>(current_eval_line);

        if (!line_id_comp) {
            log_lp_info(scheduler_, "拓扑警告: 'is_fault_path_energized' - 在路径检查中未找到线路 %llu (%s) 的组件. 假设故障路径带电 (保守处理).",
                (unsigned long long)current_eval_line, current_eval_line_name_str.c_str());
            return true;
        }
        log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 当前评估线路 %llu (%s), 检查其出口断路器 %llu.",
            (unsigned long long)current_eval_line, current_eval_line_name_str.c_str(),
            (unsigned long long)line_id_comp->associated_breaker_entity);

        auto breaker_state = registry_.get<BreakerStateComponent>(line_id_comp->associated_breaker_entity);
        std::string breaker_name_str = get_safe_entity_name(line_id_comp->associated_breaker_entity, "BreakerIdentityComponent", "关联断路器");

        if (!breaker_state) {
            log_lp_info(scheduler_, "拓扑警告: 'is_fault_path_energized' - 未找到断路器 %llu (%s) 的BreakerStateComponent. 假设闭合 (保守处理).",
                (unsigned long long)line_id_comp->associated_breaker_entity, breaker_name_str.c_str());
            // Assume closed if state unknown, to be conservative for protection logic
        } else if (breaker_state->is_open) {
            log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' => 发现线路 %llu (%s) 的出口断路器 %llu (%s) 已打开. 结论: 路径已去能.",
                (unsigned long long)current_eval_line, current_eval_line_name_str.c_str(),
                (unsigned long long)line_id_comp->associated_breaker_entity, breaker_name_str.c_str());
            return false;
        }
        // Log breaker state if not open or unknown
        if (breaker_state) { // Only log if component exists
            log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 断路器 %llu (%s) 状态: %s.",
                (unsigned long long)line_id_comp->associated_breaker_entity, breaker_name_str.c_str(),
                breaker_state->is_open ? "打开" : "闭合");
        }

        if (!line_id_comp->downstream_line_entity) {
            log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 在线路 %llu (%s) 到达路径末端，但未到达故障线路 %llu (%s). 这通常意味着故障不在此路径下游. 假设路径带电.",
                (unsigned long long)current_eval_line, current_eval_line_name_str.c_str(),
                (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());
            return true;
        }
        current_eval_line = line_id_comp->downstream_line_entity.value();
        std::string next_eval_line_name_str = get_safe_entity_name(current_eval_line, "LineIdentityComponent", "下一评估线路");
        log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 移动到下一条下游线路 %llu (%s).",
            (unsigned long long)current_eval_line, next_eval_line_name_str.c_str());
    }

    if (current_eval_line == fault_on_line_entity) {
        log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 已到达故障线路 %llu (%s). 检查其自身出口断路器.",
            (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());

        auto faulted_line_id_comp = registry_.get<LineIdentityComponent>(fault_on_line_entity);
        if (faulted_line_id_comp) {
            Entity associated_breaker_of_faulted_line = faulted_line_id_comp->associated_breaker_entity;
            auto breaker_state = registry_.get<BreakerStateComponent>(associated_breaker_of_faulted_line);
            std::string breaker_name_str = get_safe_entity_name(associated_breaker_of_faulted_line, "BreakerIdentityComponent", "故障线路的断路器");

            if (!breaker_state) {
                log_lp_info(scheduler_, "拓扑警告: 'is_fault_path_energized' - 未找到故障线路 %llu (%s) 的断路器 %llu (%s) 的BreakerStateComponent. 假设闭合.",
                    (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str(),
                    (unsigned long long)associated_breaker_of_faulted_line, breaker_name_str.c_str());
            } else if (breaker_state->is_open) {
                log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' => 发现故障线路 %llu (%s) 自身的出口断路器 %llu (%s) 已打开. 结论: 故障已被其自身断路器隔离.",
                    (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str(),
                    (unsigned long long)associated_breaker_of_faulted_line, breaker_name_str.c_str());
                return false;
            }
            if (breaker_state) { // Only log if component exists
                log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' - 故障线路 %llu (%s) 自身的出口断路器 %llu (%s) 状态: %s. 故障未被此断路器隔离.",
                    (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str(),
                    (unsigned long long)associated_breaker_of_faulted_line, breaker_name_str.c_str(),
                    breaker_state->is_open ? "打开" : "闭合");
            }

        } else {
            log_lp_info(scheduler_, "拓扑警告: 'is_fault_path_energized' - 未找到故障线路 %llu (%s) 的LineIdentityComponent. 假设路径带电.",
                (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());
            return true;
        }
    } else if (safety_count >= 10) {
        log_lp_info(scheduler_, "拓扑警告: 'is_fault_path_energized' - 路径追踪超出安全限制 (%d次迭代) 从 %llu (%s) 到 %llu (%s). 假设路径带电.",
            safety_count,
            (unsigned long long)perspective_line_entity, perspective_line_name_str.c_str(),
            (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());
        return true;
    }

    log_lp_info(scheduler_, "拓扑主检查: 'is_fault_path_energized' => 未在路径上发现隔离断路器. 从线路 %llu (%s) 的视角看，到线路 %llu (%s) 的故障路径仍然带电. 结论: 路径带电.",
        (unsigned long long)perspective_line_entity, perspective_line_name_str.c_str(),
        (unsigned long long)fault_on_line_entity, fault_line_name_str.c_str());
    return true;
}

// 单个保护装置逻辑的协程任务
cps_coro::Task LogicProtectionSystem::protection_device_logic_task(Entity protection_entity)
{
    auto prot_comp = registry_.get<ProtectionDeviceComponent>(protection_entity);
    if (!prot_comp) {
        log_lp_info(scheduler_, "致命错误: 未找到实体 %llu 的 ProtectionDeviceComponent 组件. 保护任务无法启动.", (unsigned long long)protection_entity);
        co_return;
    }

    // Safely get protection name for logging
    std::string prot_name_str = "未知保护(名称获取失败)";
    try {
        prot_name_str = prot_comp->name;
    } catch (const std::exception&) {
    }

    log_lp_info(scheduler_, "保护任务 '%s' (实体 %llu) 已启动. 类型: %s, 延时: %lldms. 监视线路 %llu, 控制断路器 %llu.",
        prot_name_str.c_str(), (unsigned long long)protection_entity,
        (prot_comp->type == ProtectionDeviceComponent::Type::MAIN ? "主保护" : "后备保护"),
        (long long)prot_comp->trip_delay.count(),
        (unsigned long long)prot_comp->protected_line_entity,
        (unsigned long long)prot_comp->commanded_breaker_entity);

    while (true) {
        // Update name in case it was corrupted and fixed, or for safety in loop
        try {
            prot_name_str = prot_comp->name;
        } catch (const std::exception&) {
            prot_name_str = "未知保护(名称访问异常)";
        }

        if (prot_comp->current_state == ProtectionDeviceComponent::State::IDLE || prot_comp->current_state == ProtectionDeviceComponent::State::RESETTING) {
            if (prot_comp->current_state == ProtectionDeviceComponent::State::RESETTING) {
                prot_comp->current_state = ProtectionDeviceComponent::State::IDLE;
                prot_comp->fault_currently_seen_on_line = std::nullopt;
                log_lp_info(scheduler_, "'%s' (实体 %llu) 已完成复归, 进入 空闲 状态.", prot_name_str.c_str(), (unsigned long long)protection_entity);
            }

            log_lp_info(scheduler_, "'%s' (实体 %llu) 处于 空闲 状态, 等待逻辑故障事件 (LOGIC_FAULT_EVENT).", prot_name_str.c_str(), (unsigned long long)protection_entity);
            LogicFaultInfo fault_info = co_await cps_coro::wait_for_event<LogicFaultInfo>(LOGIC_FAULT_EVENT);
            prot_comp->fault_currently_seen_on_line = fault_info.faulted_line_entity;
            log_lp_info(scheduler_, "'%s' (实体 %llu) 收到逻辑故障事件. 故障发生在 %llu 号线路上.", prot_name_str.c_str(), (unsigned long long)protection_entity, (unsigned long long)fault_info.faulted_line_entity);

            bool is_relevant_fault = false;
            log_lp_info(scheduler_, "'%s' (实体 %llu) 开始判断故障线路 %llu 是否在其保护范围内.", prot_name_str.c_str(), (unsigned long long)protection_entity, (unsigned long long)fault_info.faulted_line_entity);

            if (prot_comp->type == ProtectionDeviceComponent::Type::MAIN) {
                if (fault_info.faulted_line_entity == prot_comp->protected_line_entity) {
                    is_relevant_fault = true;
                    log_lp_info(scheduler_, "'%s' (主保护) 判断: 故障在线路 %llu 上, 与其直接保护线路 %llu 匹配. 故障相关.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity, (unsigned long long)prot_comp->protected_line_entity);
                } else {
                    log_lp_info(scheduler_, "'%s' (主保护) 判断: 故障在线路 %llu 上, 与其直接保护线路 %llu 不匹配. 故障不相关.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity, (unsigned long long)prot_comp->protected_line_entity);
                }
            } else { // BACKUP
                log_lp_info(scheduler_, "'%s' (后备保护) 检查故障线路 %llu 是否在其保护线路 %llu 或其下游.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity, (unsigned long long)prot_comp->protected_line_entity);
                if (is_line_downstream_or_same(prot_comp->protected_line_entity, fault_info.faulted_line_entity)) {
                    log_lp_info(scheduler_, "'%s' (后备保护): 故障线路 %llu 位于其保护范围 (线路 %llu) 或下游. 进一步检查路径是否带电.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity, (unsigned long long)prot_comp->protected_line_entity);
                    if (is_fault_path_energized(prot_comp->protected_line_entity, fault_info.faulted_line_entity)) {
                        is_relevant_fault = true;
                        log_lp_info(scheduler_, "'%s' (后备保护) 判断: 故障在线路 %llu 上, 且路径带电. 故障相关.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity);
                    } else {
                        log_lp_info(scheduler_, "'%s' (后备保护): 故障线路 %llu 位于其保护范围或下游, 但路径已被去能. 故障不相关或无需此保护动作.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity);
                    }
                } else {
                    log_lp_info(scheduler_, "'%s' (后备保护) 判断: 故障线路 %llu 不在其保护线路 %llu 或其下游. 故障不相关.", prot_name_str.c_str(), (unsigned long long)fault_info.faulted_line_entity, (unsigned long long)prot_comp->protected_line_entity);
                }
            }

            if (is_relevant_fault) {
                prot_comp->current_state = ProtectionDeviceComponent::State::PICKED_UP;
                log_lp_info(scheduler_, "'%s' (实体 %llu) 已启动 (PICKED_UP), 针对线路 %llu 上的故障. 准备开始 %lldms 计时.",
                    prot_name_str.c_str(), (unsigned long long)protection_entity, (unsigned long long)fault_info.faulted_line_entity, (long long)prot_comp->trip_delay.count());
            } else {
                prot_comp->fault_currently_seen_on_line = std::nullopt;
                log_lp_info(scheduler_, "'%s' (实体 %llu) 判断故障不相关, 继续保持 空闲 状态.", prot_name_str.c_str(), (unsigned long long)protection_entity);
            }
        } else if (prot_comp->current_state == ProtectionDeviceComponent::State::PICKED_UP) {
            log_lp_info(scheduler_, "'%s' (实体 %llu) 状态从 PICKED_UP 转换为 TRIPPING_TIMER_RUNNING. 开始延时 %lldms.", prot_name_str.c_str(), (unsigned long long)protection_entity, (long long)prot_comp->trip_delay.count());
            prot_comp->current_state = ProtectionDeviceComponent::State::TRIPPING_TIMER_RUNNING;
            co_await cps_coro::delay(prot_comp->trip_delay);

            if (!prot_comp->fault_currently_seen_on_line) {
                log_lp_info(scheduler_, "警告: '%s' (实体 %llu) 计时结束但 fault_currently_seen_on_line 为空. 返回空闲状态.", prot_name_str.c_str(), (unsigned long long)protection_entity);
                prot_comp->current_state = ProtectionDeviceComponent::State::RESETTING;
                continue;
            }
            Entity fault_line_when_timer_started = prot_comp->fault_currently_seen_on_line.value();
            log_lp_info(scheduler_, "'%s' (实体 %llu) 跳闸计时结束. 重新检查原故障线路 %llu (从保护安装点 %llu 视角) 是否仍然带电.",
                prot_name_str.c_str(), (unsigned long long)protection_entity,
                (unsigned long long)fault_line_when_timer_started,
                (unsigned long long)prot_comp->protected_line_entity);

            if (is_fault_path_energized(prot_comp->protected_line_entity, fault_line_when_timer_started)) {
                log_lp_info(scheduler_, "'%s' (实体 %llu) 确认线路 %llu 上的故障仍然带电. 正在向断路器 %llu 发送跳闸命令.",
                    prot_name_str.c_str(), (unsigned long long)protection_entity,
                    (unsigned long long)fault_line_when_timer_started,
                    (unsigned long long)prot_comp->commanded_breaker_entity);
                scheduler_.trigger_event(LOGIC_BREAKER_TRIP_COMMAND_EVENT, LogicBreakerCommand { prot_comp->commanded_breaker_entity });
                prot_comp->current_state = ProtectionDeviceComponent::State::TRIPPED;
                log_lp_info(scheduler_, "'%s' (实体 %llu) 状态转换为 TRIPPED.", prot_name_str.c_str(), (unsigned long long)protection_entity);
            } else {
                log_lp_info(scheduler_, "'%s' (实体 %llu) 检测到线路 %llu 上的故障在其计时期间已被其他装置隔离或消失. 正在复归.",
                    prot_name_str.c_str(), (unsigned long long)protection_entity, (unsigned long long)fault_line_when_timer_started);
                prot_comp->current_state = ProtectionDeviceComponent::State::RESETTING;
            }
        } else if (prot_comp->current_state == ProtectionDeviceComponent::State::TRIPPED) {
            log_lp_info(scheduler_, "'%s' (实体 %llu) 当前处于 TRIPPED 状态. 准备进入 RESETTING 状态.", prot_name_str.c_str(), (unsigned long long)protection_entity);
            prot_comp->current_state = ProtectionDeviceComponent::State::RESETTING;
            co_await cps_coro::delay(std::chrono::milliseconds(10));
        } else if (prot_comp->current_state == ProtectionDeviceComponent::State::RESETTING) {
            // This state transition is handled at the top of the loop.
            // Add a small delay to yield execution, allowing the loop to restart and process the state change.
            co_await cps_coro::delay(std::chrono::milliseconds(1));
        } else { // Unknown state
            log_lp_info(scheduler_, "警告: '%s' (实体 %llu) 进入未知状态 %d. 将等待100ms后重试.", prot_name_str.c_str(), (unsigned long long)protection_entity, static_cast<int>(prot_comp->current_state));
            co_await cps_coro::delay(std::chrono::milliseconds(100));
        }
    }
}

// 单个断路器逻辑的协程任务
cps_coro::Task LogicProtectionSystem::breaker_logic_task(Entity breaker_entity)
{
    auto breaker_id_comp = registry_.get<BreakerIdentityComponent>(breaker_entity);
    auto breaker_state_comp = registry_.get<BreakerStateComponent>(breaker_entity);

    if (!breaker_id_comp || !breaker_state_comp) {
        log_lp_info(scheduler_, "致命错误: 未找到实体 %llu 的断路器组件 (ID或State). 断路器任务无法启动.", (unsigned long long)breaker_entity);
        co_return;
    }

    std::string breaker_name_str = "未知断路器(名称获取失败)";
    try {
        breaker_name_str = breaker_id_comp->name;
    } catch (const std::exception&) {
    }

    log_lp_info(scheduler_, "断路器任务 '%s' (实体 %llu) 已启动. 是否拒动: %s. 初始状态: %s.",
        breaker_name_str.c_str(), (unsigned long long)breaker_entity,
        breaker_id_comp->is_stuck_on_trip_cmd ? "是" : "否",
        breaker_state_comp->is_open ? "打开" : "闭合");

    while (true) {
        try {
            breaker_name_str = breaker_id_comp->name;
        } catch (const std::exception&) {
            breaker_name_str = "未知断路器(名称访问异常)";
        }

        log_lp_info(scheduler_, "'%s' (实体 %llu) 等待跳闸命令 (LOGIC_BREAKER_TRIP_COMMAND_EVENT). 当前状态: %s.",
            breaker_name_str.c_str(), (unsigned long long)breaker_entity,
            breaker_state_comp->is_open ? "打开" : "闭合");
        LogicBreakerCommand cmd = co_await cps_coro::wait_for_event<LogicBreakerCommand>(LOGIC_BREAKER_TRIP_COMMAND_EVENT);

        if (cmd.breaker_to_trip_entity == breaker_entity) {
            log_lp_info(scheduler_, "'%s' (实体 %llu) 收到针对自身的跳闸命令.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
            if (breaker_state_comp->is_open) {
                log_lp_info(scheduler_, "'%s' (实体 %llu) 已经是打开状态. 无需操作.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
            } else {
                if (breaker_id_comp->is_stuck_on_trip_cmd) {
                    log_lp_info(scheduler_, "'%s' (实体 %llu) 发生拒动! 保持闭合状态.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
                } else {
                    log_lp_info(scheduler_, "'%s' (实体 %llu) 准备执行打开操作, 模拟延时 20ms.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
                    co_await cps_coro::delay(std::chrono::milliseconds(20));
                    breaker_state_comp->is_open = true;
                    log_lp_info(scheduler_, "'%s' (实体 %llu) 已成功打开.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
                    log_lp_info(scheduler_, "'%s' (实体 %llu) 触发断路器状态变更事件 (LOGIC_BREAKER_STATUS_CHANGED_EVENT): 已打开.", breaker_name_str.c_str(), (unsigned long long)breaker_entity);
                    scheduler_.trigger_event(LOGIC_BREAKER_STATUS_CHANGED_EVENT, LogicBreakerStatus { breaker_entity, true });
                }
            }
        } else {
            log_lp_info(scheduler_, "'%s' (实体 %llu) 收到跳闸命令, 但目标是断路器 %llu, 非本断路器. 忽略.",
                breaker_name_str.c_str(), (unsigned long long)breaker_entity, (unsigned long long)cmd.breaker_to_trip_entity);
        }
    }
}

// 模拟永久性故障及断路器拒动场景的主协程
cps_coro::Task LogicProtectionSystem::simulate_permanent_fault_with_breaker_failure_scenario()
{
    log_lp_info(scheduler_, "开始执行保护故障场景仿真...");

    double initial_delay_ms = 1000.0;
    log_lp_info(scheduler_, "主场景: 等待 %.0fms.", initial_delay_ms);
    co_await cps_coro::delay(std::chrono::milliseconds(static_cast<long long>(initial_delay_ms)));

    log_lp_info(scheduler_, "主场景: 步骤 1 - 正在向线路C (ID: %llu) 注入永久性故障.", (unsigned long long)line_C_entity);
    scheduler_.trigger_event(LOGIC_FAULT_EVENT, LogicFaultInfo { line_C_entity });

    log_lp_info(scheduler_, "主场景: 故障已注入. 等待保护系统响应...");
    auto wait_duration_for_sequence = std::chrono::milliseconds(4000);
    log_lp_info(scheduler_, "主场景: 等待 %lldms 以便观察保护序列的完整过程.", (long long)wait_duration_for_sequence.count());
    co_await cps_coro::delay(wait_duration_for_sequence);

    log_lp_info(scheduler_, "主场景: 场景发展时间已过 (%.0fms + %lldms), 正在验证最终状态...", initial_delay_ms, (long long)wait_duration_for_sequence.count());

    // Helper for safe name getting for final status
    auto get_name_or_default = [&](Entity entity, const char* type_default) -> std::string {
        std::string name_str = std::string(type_default) + " (ID: " + std::to_string(entity) + ", 名称获取失败)";
        if (entity == 0)
            return std::string(type_default) + " (ID 0)";
        try {
            if (auto* comp = registry_.get<BreakerIdentityComponent>(entity)) {
                name_str = comp->name;
            } else if (auto* comp = registry_.get<ProtectionDeviceComponent>(entity)) {
                name_str = comp->name;
            }
            // Add other component types if needed for name lookup
        } catch (const std::exception&) {
        }
        return name_str;
    };

    auto state_cb_c = registry_.get<BreakerStateComponent>(breaker_C_entity);
    auto id_cb_c = registry_.get<BreakerIdentityComponent>(breaker_C_entity);
    auto state_cb_b = registry_.get<BreakerStateComponent>(breaker_B_entity);
    auto id_cb_b = registry_.get<BreakerIdentityComponent>(breaker_B_entity);
    auto state_cb_a = registry_.get<BreakerStateComponent>(breaker_A_entity);
    auto id_cb_a = registry_.get<BreakerIdentityComponent>(breaker_A_entity);

    auto prot_main_c_comp = registry_.get<ProtectionDeviceComponent>(prot_main_C_entity);
    auto prot_backup_b_comp = registry_.get<ProtectionDeviceComponent>(prot_backup_B_entity);
    auto prot_backup_a_comp = registry_.get<ProtectionDeviceComponent>(prot_backup_A_entity);

    const char* protection_state_names[] = { "空闲(IDLE)", "已启动(PICKED_UP)", "计时中(TIMER)", "已跳闸(TRIPPED)", "复归中(RESET)" };

    if (state_cb_c && id_cb_c)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu, %s) 状态: %s.",
            get_name_or_default(breaker_C_entity, "断路器C").c_str(), (unsigned long long)breaker_C_entity,
            id_cb_c->is_stuck_on_trip_cmd ? "拒动" : "正常",
            state_cb_c->is_open ? "打开" : "闭合");
    if (state_cb_b && id_cb_b)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu, %s) 状态: %s.",
            get_name_or_default(breaker_B_entity, "断路器B").c_str(), (unsigned long long)breaker_B_entity,
            id_cb_b->is_stuck_on_trip_cmd ? "拒动" : "正常",
            state_cb_b->is_open ? "打开" : "闭合");
    if (state_cb_a && id_cb_a)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu, %s) 状态: %s.",
            get_name_or_default(breaker_A_entity, "断路器A").c_str(), (unsigned long long)breaker_A_entity,
            id_cb_a->is_stuck_on_trip_cmd ? "拒动" : "正常",
            state_cb_a->is_open ? "打开" : "闭合");

    if (prot_main_c_comp)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu) 状态: %s.",
            get_name_or_default(prot_main_C_entity, "C主保护").c_str(), (unsigned long long)prot_main_C_entity,
            protection_state_names[static_cast<int>(prot_main_c_comp->current_state)]);
    if (prot_backup_b_comp)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu) 状态: %s.",
            get_name_or_default(prot_backup_B_entity, "B后备保护").c_str(), (unsigned long long)prot_backup_B_entity,
            protection_state_names[static_cast<int>(prot_backup_b_comp->current_state)]);
    if (prot_backup_a_comp)
        log_lp_info(scheduler_, "最终状态: %s (ID: %llu) 状态: %s.",
            get_name_or_default(prot_backup_A_entity, "A后备保护").c_str(), (unsigned long long)prot_backup_A_entity,
            protection_state_names[static_cast<int>(prot_backup_a_comp->current_state)]);

    log_lp_info(scheduler_, "保护故障场景仿真协程执行完毕.");
    co_return;
}