#ifndef LOGIC_PROTECTION_SYSTEM_H
#define LOGIC_PROTECTION_SYSTEM_H

#include "PowerSystemTopology.h"
#include "cps_coro_lib.h"
#include "ecs_core.h"
#include "simulation_events_and_data.h"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ReconfigurationOption {
    Entity breaker_to_close = 0;
    int path_length = std::numeric_limits<int>::max(); // 路径成本，越小越好
};

// --- 组件定义 ---

struct BusIdentityComponent : public IComponent {
    std::string name;
    bool is_power_source = false;
    explicit BusIdentityComponent(std::string n, bool source = false)
        : name(std::move(n))
        , is_power_source(source)
    {
    }
};

struct LineIdentityComponent : public IComponent {
    std::string name;
    Entity from_bus_entity;
    Entity to_bus_entity;
    LineIdentityComponent(std::string n, Entity from, Entity to)
        : name(std::move(n))
        , from_bus_entity(from)
        , to_bus_entity(to)
    {
    }
};

struct BreakerIdentityComponent : public IComponent {
    std::string name;
    Entity associated_line_entity;
    Entity connected_bus_entity;
    bool is_stuck_on_trip_cmd = false;

    BreakerIdentityComponent(std::string n, Entity line, Entity bus, bool stuck = false)
        : name(std::move(n))
        , associated_line_entity(line)
        , connected_bus_entity(bus)
        , is_stuck_on_trip_cmd(stuck)
    {
    }
};

struct BreakerStateComponent : public IComponent {
    bool is_open = false;
    bool is_normally_open = false;
    BreakerStateComponent(bool open = false, bool normally_open = false)
        : is_open(open)
        , is_normally_open(normally_open)
    {
    }
};

struct ProtectionDeviceComponent : public IComponent {
    enum class Type { MAIN,
        BACKUP };
    std::string name;
    Type type;
    std::vector<Entity> protected_entities; // 主保护的线路
    std::vector<Entity> backup_protected_entities; // 作为后备保护的线路
    std::vector<Entity> commanded_breaker_entities; // 控制的断路器
    cps_coro::Scheduler::duration trip_delay;

    ProtectionDeviceComponent(std::string n, Type t, std::vector<Entity> p_entities,
        std::vector<Entity> c_breakers, int delay_ms,
        std::vector<Entity> b_entities = {}) // 增加后备保护线路列表
        : name(std::move(n))
        , type(t)
        , protected_entities(std::move(p_entities))
        , commanded_breaker_entities(std::move(c_breakers))
        , trip_delay(std::chrono::milliseconds(delay_ms))
        , backup_protected_entities(std::move(b_entities))
    {
    }
};
class LogicProtectionSystem {
public:
    LogicProtectionSystem(Registry& registry, cps_coro::Scheduler& scheduler);
    void initialize_scenario_entities();
    cps_coro::Task simulate_fault_and_reconfiguration_scenario();

private:
    Registry& registry_;
    cps_coro::Scheduler& scheduler_;
    PowerSystemTopology topology_; // 拓扑接口

    std::unordered_map<std::string, Entity> bus_entities;
    std::unordered_map<std::string, Entity> line_entities;
    std::unordered_map<std::string, Entity> breaker_entities;
    std::unordered_map<std::string, Entity> protection_entities;
    Entity reconfig_system_entity;

    //  用于存储当前活动故障的成员变量
    Entity active_fault_line_ = 0;

    // 协程任务
    cps_coro::Task protection_device_logic_task(Entity protection_entity);
    cps_coro::Task breaker_logic_task(Entity breaker_entity);
    cps_coro::Task network_reconfiguration_logic_task();
    cps_coro::Task supply_check_task(Entity bus_entity_to_check);

    // 辅助函数
    bool is_bus_connected_to_source(BusId target_bus);
    bool is_line_energized(Entity line_entity);
    std::vector<BranchId> get_currently_open_lines();

    // 动态决策函数现在需要知道哪个是故障线路
    std::optional<ReconfigurationOption> find_reconfiguration_option(Entity lost_bus_entity, Entity faulted_line);
};

#endif // LOGIC_PROTECTION_SYSTEM_H