#ifndef POWER_SYSTEM_TOPOLOGY_H
#define POWER_SYSTEM_TOPOLOGY_H

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// --- 类型别名，采用电力系统术语，增强代码可读性 ---
using BusId = int; // 母线ID (对应图论中的节点)
using BranchId = int; // 支路ID (对应图论中的边)

// --- 邻接信息结构体 ---
// 存储邻接表中的一个元素，代表一条相连的支路。
struct AdjacencyInfo {
    BranchId branch_id; // 这条连接对应的支路ID
    int internal_bus_idx; // 这条支路对侧母线的内部索引
};

// --- 路径搜索结果结构体 ---
// 封装路径搜索结果，比返回多个vector更清晰。
struct Path {
    std::vector<BusId> buses; // 路径经过的母线ID列表
    std::vector<BranchId> branches; // 路径经过的支路ID列表
};

/**
 * @class PowerSystemTopology
 * @brief 一个通用的电力系统拓扑分析类
 *
 * 该类封装了电力系统网络拓扑的常用分析算法，旨在为电力工程师提供
 * 一套易于理解和使用的工具。
 *
 * 核心功能包括:
 * - 基础分析: 电气岛划分、两点间路径搜索
 * - 结构脆弱性分析: 查找关键线路（割边）、查找关键母线（割点）
 * - 网络特性识别: 计算母线连接度、识别网络中的所有环路、检测辐射状接线
 * - 动态模拟: 支持在线路投退（移除/添加支路）后快速重新分析
 *
 * 内部采用邻接表存储拓扑，并使用哈希表将外部任意整数母线ID映射到内部
 * 连续索引，兼顾了灵活性和算法效率。
 */
class PowerSystemTopology {
public:
    PowerSystemTopology() = default;

    // --- 拓扑构建 ---
    /**
     * @brief 构建或重建电网拓扑模型
     * @param bus_ids 电网中所有母线的ID列表
     * @param branch_ids 所有支路的ID列表
     * @param branch_endpoints 支路连接的两个母线ID对，与branch_ids一一对应
     */
    void buildTopology(
        const std::vector<BusId>& bus_ids,
        const std::vector<BranchId>& branch_ids,
        const std::vector<std::pair<BusId, BusId>>& branch_endpoints);

    // --- 核心拓扑分析功能 ---
    /**
     * @brief 1. 电气岛分析 (Connectivity Analysis)
     * @details 识别网络中所有独立的、互不连通的子网络。
     * @param[out] island_count 计算出的总电气岛数量
     * @return std::unordered_map<BusId, int> 从母线ID到其所属电气岛索引(0-based)的映射
     */
    std::unordered_map<BusId, int> findElectricalIslands(int& island_count) const;

    /**
     * @brief 2. 路径搜索 (Path Finding)
     * @details 查找两个母线之间的电气路径。
     * @param start_bus 起始母线ID
     * @param end_bus 终止母线ID
     * @param open_branches (可选) 模拟断开的支路ID列表，在搜索中将被忽略
     * @return std::optional<Path> 若找到路径，返回包含路径信息的Path对象；否则返回std::nullopt
     */
    std::optional<Path> findPath(
        BusId start_bus,
        BusId end_bus,
        const std::vector<BranchId>& open_branches = {}) const;

    /**
     * @brief 3. 查找关键线路 (Find Critical Lines / Bridges)
     * @details 识别网络中的割边。这些线路一旦断开，会导致电网解列，形成新的电气岛。
     * @return std::vector<BranchId> 所有关键线路的ID列表
     */
    std::vector<BranchId> findCriticalLines() const;

    /**
     * @brief 4. 查找关键母线 (Find Critical Buses / Articulation Points)
     * @details 识别网络中的割点。这些母线（或厂站）一旦停运，会导致与之相连的部分网络失去联系。
     * @return std::vector<BusId> 所有关键母线的ID列表
     */
    std::vector<BusId> findCriticalBuses() const;

    /**
     * @brief 5. 查找所有环路 (Find All Loops / Cycles)
     * @details 识别网络中所有的闭环结构。对于大型密集网络，此函数可能非常耗时。
     * @return std::vector<std::vector<BusId>> 每个内部vector代表一个环路上的母线列表
     */
    std::vector<std::vector<BusId>> findAllLoops() const;

    /**
     * @brief 6. 计算母线连接度 (Bus Degree)
     * @details 计算每条母线连接的支路数量。
     * @return std::unordered_map<BusId, int> 从母线ID到其连接度的映射
     */
    std::unordered_map<BusId, int> getBusDegrees() const;

    /**
     * @brief 7. 辐射状网络检测 (Radial Network Detection)
     * @details 判断每个电气岛是否为无环的辐射状（树状）结构。
     * @return std::unordered_map<int, bool> 从电气岛索引到其是否为辐射状的布尔值映射
     */
    std::unordered_map<int, bool> checkRadialIslands() const;

    /**
     * @brief 8. 潮流追溯 (Power Flow Tracing)
     * @details 从一个给定的起始母线，追溯其所有上游（朝向电源）或下游（远离电源）的设备。
     * @param start_bus 追溯的起始母线ID。
     * @param source_buses 电源母线列表，用于确定整个网络的潮流方向基准。
     * @param trace_downstream 追溯方向。true表示向下游追溯（默认），false表示向上游追溯。
     * @return Path 包含所有被追溯到的母线和支路集合（结果无序）。
     */
    Path tracePowerFlow(
        BusId start_bus,
        const std::vector<BusId>& source_buses,
        bool trace_downstream = true) const;

    // --- 动态修改功能 ---
    /**
     * @brief 9. 断开支路 (Open Branch)
     * @param branch_id_to_open 要断开的支路ID
     * @return bool 如果成功断开返回true，如果支路不存在则返回false
     */
    bool openBranch(BranchId branch_id_to_open);

    // --- 工具函数 ---
    bool isReady() const { return !adjacency_list.empty(); }
    int getBusCount() const { return internal_idx_to_bus_id.size(); }

private:
    // --- 内部数据结构 ---
    std::vector<std::vector<AdjacencyInfo>> adjacency_list; // 核心数据结构：邻接表
    std::unordered_map<BusId, int> bus_to_internal_idx; // 映射: 外部母线ID -> 内部索引
    std::vector<BusId> internal_idx_to_bus_id; // 映射: 内部索引 -> 外部母线ID
    std::unordered_map<BranchId, std::pair<BusId, BusId>> branch_endpoints_map; // 存储支路及其两端母线

    // --- 内部辅助函数 ---
    int getBusInternalIndex(BusId bus_id) const;
    void findCriticalLinesUtil(int u, std::vector<int>& disc, std::vector<int>& low, std::vector<int>& parent, std::vector<BranchId>& bridges, int& time) const;
    void findCriticalBusesUtil(int u, std::vector<int>& disc, std::vector<int>& low, std::vector<int>& parent, std::vector<bool>& is_ap, int& time) const;
    void findAllLoopsUtil(int u, int p, std::vector<int>& color, std::vector<int>& path, std::vector<std::vector<int>>& cycles_internal) const;

    // 禁止拷贝和赋值，因为该对象管理着复杂的内部状态，浅拷贝会导致问题。
    PowerSystemTopology(const PowerSystemTopology&) = delete;
    PowerSystemTopology& operator=(const PowerSystemTopology&) = delete;
};

#endif // POWER_SYSTEM_TOPOLOGY_H