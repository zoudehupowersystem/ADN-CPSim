#include "PowerSystemTopology.h"
#include <algorithm>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <unordered_set>

// --- 拓扑构建 ---
void PowerSystemTopology::buildTopology(
    const std::vector<BusId>& bus_ids,
    const std::vector<BranchId>& branch_ids,
    const std::vector<std::pair<BusId, BusId>>& branch_endpoints)
{
    if (branch_ids.size() != branch_endpoints.size()) {
        throw std::invalid_argument("错误: 支路ID数量与支路端点对数量不匹配。");
    }

    adjacency_list.clear();
    bus_to_internal_idx.clear();
    internal_idx_to_bus_id.clear();
    branch_endpoints_map.clear();

    internal_idx_to_bus_id = bus_ids;
    bus_to_internal_idx.reserve(bus_ids.size());
    for (size_t i = 0; i < bus_ids.size(); ++i) {
        bus_to_internal_idx[bus_ids[i]] = i;
    }

    adjacency_list.assign(getBusCount(), std::vector<AdjacencyInfo>());
    for (auto& bus_connections : adjacency_list) {
        bus_connections.reserve(6); // 基于电力系统母线平均连接度进行的性能优化
    }

    for (size_t i = 0; i < branch_ids.size(); ++i) {
        BusId bus1_id = branch_endpoints[i].first;
        BusId bus2_id = branch_endpoints[i].second;
        BranchId branch_id = branch_ids[i];

        auto it1 = bus_to_internal_idx.find(bus1_id);
        auto it2 = bus_to_internal_idx.find(bus2_id);

        if (it1 == bus_to_internal_idx.end() || it2 == bus_to_internal_idx.end()) {
            std::cerr << "警告: 支路 " << branch_id << " 连接了未在母线列表中定义的母线。该支路将被忽略。" << std::endl;
            continue;
        }

        int u_idx = it1->second;
        int v_idx = it2->second;

        adjacency_list[u_idx].push_back({ branch_id, v_idx });
        adjacency_list[v_idx].push_back({ branch_id, u_idx });

        branch_endpoints_map[branch_id] = { bus1_id, bus2_id };
    }
}

// --- 内部辅助函数 ---
int PowerSystemTopology::getBusInternalIndex(BusId bus_id) const
{
    auto it = bus_to_internal_idx.find(bus_id);
    if (it == bus_to_internal_idx.end()) {
        return -1; // 表示母线不存在
    }
    return it->second;
}

// --- 1. 电气岛分析 ---
std::unordered_map<BusId, int> PowerSystemTopology::findElectricalIslands(int& island_count) const
{
    if (!isReady()) {
        island_count = 0;
        return {};
    }

    island_count = 0;
    std::vector<int> visited(getBusCount(), 0); // 0: 未访问, >0: 岛屿ID(1-based)
    std::unordered_map<BusId, int> result;
    result.reserve(getBusCount());

    for (int i = 0; i < getBusCount(); ++i) {
        if (visited[i] == 0) {
            island_count++;
            std::queue<int> q;
            q.push(i);
            visited[i] = island_count;

            while (!q.empty()) {
                int u_idx = q.front();
                q.pop();

                for (const auto& conn : adjacency_list[u_idx]) {
                    int v_idx = conn.internal_bus_idx;
                    if (visited[v_idx] == 0) {
                        visited[v_idx] = island_count;
                        q.push(v_idx);
                    }
                }
            }
        }
    }

    for (int i = 0; i < getBusCount(); ++i) {
        result[internal_idx_to_bus_id[i]] = visited[i] - 1; // 转为0-based
    }
    return result;
}

// --- 2. 路径搜索 ---
std::optional<Path> PowerSystemTopology::findPath(
    BusId start_bus,
    BusId end_bus,
    const std::vector<BranchId>& open_branches) const
{
    int start_idx = getBusInternalIndex(start_bus);
    int end_idx = getBusInternalIndex(end_bus);

    if (start_idx == -1 || end_idx == -1)
        return std::nullopt;
    if (start_idx == end_idx)
        return Path { { start_bus }, {} };

    std::unordered_set<BranchId> open_set(open_branches.begin(), open_branches.end());
    std::vector<std::pair<int, BranchId>> predecessor(getBusCount(), { -1, -1 });
    std::vector<bool> visited(getBusCount(), false);
    std::queue<int> q;

    q.push(start_idx);
    visited[start_idx] = true;
    bool found = false;

    while (!q.empty()) {
        int u_idx = q.front();
        q.pop();

        if (u_idx == end_idx) {
            found = true;
            break;
        }

        for (const auto& conn : adjacency_list[u_idx]) {
            if (open_set.count(conn.branch_id))
                continue;

            int v_idx = conn.internal_bus_idx;
            if (!visited[v_idx]) {
                visited[v_idx] = true;
                predecessor[v_idx] = { u_idx, conn.branch_id };
                q.push(v_idx);
            }
        }
    }

    if (!found)
        return std::nullopt;

    Path path;
    int current_idx = end_idx;
    while (current_idx != -1) {
        path.buses.push_back(internal_idx_to_bus_id[current_idx]);
        if (predecessor[current_idx].first != -1) {
            path.branches.push_back(predecessor[current_idx].second);
        }
        current_idx = predecessor[current_idx].first;
    }

    std::reverse(path.buses.begin(), path.buses.end());
    std::reverse(path.branches.begin(), path.branches.end());
    return path;
}

// --- 3. 查找关键线路 ---
std::vector<BranchId> PowerSystemTopology::findCriticalLines() const
{
    if (!isReady())
        return {};

    std::vector<int> disc(getBusCount(), -1), low(getBusCount(), -1), parent(getBusCount(), -1);
    std::vector<BranchId> critical_lines;
    int time = 0;

    for (int i = 0; i < getBusCount(); ++i) {
        if (disc[i] == -1) {
            findCriticalLinesUtil(i, disc, low, parent, critical_lines, time);
        }
    }
    return critical_lines;
}

void PowerSystemTopology::findCriticalLinesUtil(
    int u, std::vector<int>& disc, std::vector<int>& low,
    std::vector<int>& parent, std::vector<BranchId>& critical_lines, int& time) const
{
    disc[u] = low[u] = ++time;
    for (const auto& conn : adjacency_list[u]) {
        int v = conn.internal_bus_idx;
        if (v == parent[u])
            continue;

        if (disc[v] != -1) {
            low[u] = std::min(low[u], disc[v]);
        } else {
            parent[v] = u;
            findCriticalLinesUtil(v, disc, low, parent, critical_lines, time);
            low[u] = std::min(low[u], low[v]);
            if (low[v] > disc[u]) {
                critical_lines.push_back(conn.branch_id);
            }
        }
    }
}

// --- 4. 查找关键母线 ---
std::vector<BusId> PowerSystemTopology::findCriticalBuses() const
{
    if (!isReady())
        return {};

    int n = getBusCount();
    std::vector<int> disc(n, -1), low(n, -1), parent(n, -1);
    std::vector<bool> is_critical(n, false);
    int time = 0;

    for (int i = 0; i < n; ++i) {
        if (disc[i] == -1) {
            findCriticalBusesUtil(i, disc, low, parent, is_critical, time);
        }
    }

    std::vector<BusId> result;
    for (int i = 0; i < n; ++i) {
        if (is_critical[i]) {
            result.push_back(internal_idx_to_bus_id[i]);
        }
    }
    return result;
}

void PowerSystemTopology::findCriticalBusesUtil(
    int u, std::vector<int>& disc, std::vector<int>& low,
    std::vector<int>& parent, std::vector<bool>& is_critical, int& time) const
{
    disc[u] = low[u] = ++time;
    int children = 0;
    for (const auto& conn : adjacency_list[u]) {
        int v = conn.internal_bus_idx;
        if (v == parent[u])
            continue;

        if (disc[v] != -1) {
            low[u] = std::min(low[u], disc[v]);
        } else {
            children++;
            parent[v] = u;
            findCriticalBusesUtil(v, disc, low, parent, is_critical, time);
            low[u] = std::min(low[u], low[v]);
            if (parent[u] == -1 && children > 1)
                is_critical[u] = true;
            if (parent[u] != -1 && low[v] >= disc[u])
                is_critical[u] = true;
        }
    }
}

// --- 5. 查找所有环路 ---
std::vector<std::vector<BusId>> PowerSystemTopology::findAllLoops() const
{
    if (!isReady())
        return {};

    std::vector<std::vector<int>> loops_internal;
    std::vector<int> color(getBusCount(), 0);
    std::vector<int> path;

    for (int i = 0; i < getBusCount(); ++i) {
        if (color[i] == 0) {
            findAllLoopsUtil(i, -1, color, path, loops_internal);
        }
    }

    std::vector<std::vector<BusId>> result;
    for (const auto& internal_loop : loops_internal) {
        std::vector<BusId> loop;
        for (int bus_idx : internal_loop) {
            loop.push_back(internal_idx_to_bus_id[bus_idx]);
        }
        result.push_back(loop);
    }
    return result;
}

void PowerSystemTopology::findAllLoopsUtil(
    int u, int p, std::vector<int>& color, std::vector<int>& path,
    std::vector<std::vector<int>>& loops_internal) const
{
    color[u] = 1;
    path.push_back(u);

    for (const auto& conn : adjacency_list[u]) {
        int v = conn.internal_bus_idx;
        if (v == p)
            continue;

        if (color[v] == 1) {
            std::vector<int> loop;
            auto it = std::find(path.begin(), path.end(), v);
            if (it != path.end()) {
                loop.assign(it, path.end());
                std::sort(loop.begin(), loop.end());
                if (std::find(loops_internal.begin(), loops_internal.end(), loop) == loops_internal.end()) {
                    loops_internal.push_back(loop);
                }
            }
        } else if (color[v] == 0) {
            findAllLoopsUtil(v, u, color, path, loops_internal);
        }
    }

    path.pop_back();
    color[u] = 2;
}

// --- 6. 计算母线连接度 ---
std::unordered_map<BusId, int> PowerSystemTopology::getBusDegrees() const
{
    std::unordered_map<BusId, int> degrees;
    if (!isReady())
        return degrees;

    degrees.reserve(getBusCount());
    for (int i = 0; i < getBusCount(); ++i) {
        degrees[internal_idx_to_bus_id[i]] = adjacency_list[i].size();
    }
    return degrees;
}

// --- 7. 辐射状网络检测 [LOGIC CORRECTED] ---
std::unordered_map<int, bool> PowerSystemTopology::checkRadialIslands() const
{
    if (!isReady())
        return {};

    int island_count = 0;
    auto bus_to_island_map = findElectricalIslands(island_count);
    if (island_count == 0)
        return {};

    std::vector<int> buses_in_island(island_count, 0);
    std::vector<int> degree_sum_in_island(island_count, 0);

    // 统计每个岛的母线数和度数之和
    for (int i = 0; i < getBusCount(); ++i) {
        BusId bus_id = internal_idx_to_bus_id[i];
        int island_idx = bus_to_island_map[bus_id];
        buses_in_island[island_idx]++;
        degree_sum_in_island[island_idx] += adjacency_list[i].size();
    }

    std::unordered_map<int, bool> result;
    for (int i = 0; i < island_count; ++i) {
        int v_count = buses_in_island[i];
        // 根据握手定理，岛内边数 E = (岛内所有节点度数之和) / 2
        int e_count = degree_sum_in_island[i] / 2;

        // 连通图是树(辐射状)的充要条件是: 边数 E = 节点数 V - 1
        if (v_count > 0) {
            result[i] = (e_count == v_count - 1);
        }
    }
    return result;
}

// --- 8. 潮流追溯 [LOGIC CORRECTED FOR DOWNSTREAM TRACING] ---
Path PowerSystemTopology::tracePowerFlow(
    BusId start_bus,
    const std::vector<BusId>& source_buses,
    bool trace_downstream) const
{
    if (!isReady())
        return {};

    // --- 步骤 1: 从所有电源点开始全局BFS，建立父子关系 ---
    // 这个步骤保持不变，它为整个网络的潮流方向提供了基准。
    std::vector<int> parent(getBusCount(), -1);
    std::vector<bool> visited(getBusCount(), false);
    std::queue<int> q;

    for (BusId source_id : source_buses) {
        int idx = getBusInternalIndex(source_id);
        if (idx != -1 && !visited[idx]) {
            q.push(idx);
            visited[idx] = true;
        }
    }

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (const auto& conn : adjacency_list[u]) {
            int v = conn.internal_bus_idx;
            if (!visited[v]) {
                visited[v] = true;
                parent[v] = u;
                q.push(v);
            }
        }
    }

    // --- 步骤 2: 根据追溯方向进行遍历 ---
    int start_idx = getBusInternalIndex(start_bus);
    if (start_idx == -1) {
        std::cerr << "警告: 追溯的起始母线 " << start_bus << " 不在拓扑中。" << std::endl;
        return {};
    }

    Path result;

    if (trace_downstream) {
        // --- 向下游追溯 (修正后的逻辑) ---

        // Phase 1: 通过父子关系BFS，找到所有下游母线的集合
        std::unordered_set<int> downstream_bus_indices;
        std::queue<int> trace_q;

        trace_q.push(start_idx);
        downstream_bus_indices.insert(start_idx);

        while (!trace_q.empty()) {
            int u = trace_q.front();
            trace_q.pop();

            for (const auto& conn : adjacency_list[u]) {
                int v = conn.internal_bus_idx;
                // 如果v的父节点是u，说明v在u的下游
                if (parent[v] == u && downstream_bus_indices.find(v) == downstream_bus_indices.end()) {
                    downstream_bus_indices.insert(v);
                    trace_q.push(v);
                }
            }
        }

        // Phase 2: 遍历下游母线集合，找出所有连接这些母线内部的支路
        std::unordered_set<BranchId> traced_branches_set;
        for (int u_idx : downstream_bus_indices) {
            for (const auto& conn : adjacency_list[u_idx]) {
                int v_idx = conn.internal_bus_idx;
                // 如果一条支路的两端都在下游母线集合中，则该支路是下游支路
                if (downstream_bus_indices.count(v_idx)) {
                    traced_branches_set.insert(conn.branch_id);
                }
            }
        }

        // 整理结果
        result.branches.assign(traced_branches_set.begin(), traced_branches_set.end());
        for (int bus_idx : downstream_bus_indices) {
            result.buses.push_back(internal_idx_to_bus_id[bus_idx]);
        }

    } else {
        // --- 向上游追溯 (逻辑正确，保持不变) ---
        std::unordered_set<BusId> traced_buses_set;
        std::unordered_set<BranchId> traced_branches_set;

        int current_idx = start_idx;
        traced_buses_set.insert(internal_idx_to_bus_id[current_idx]);
        while (current_idx != -1 && parent[current_idx] != -1) {
            int parent_idx = parent[current_idx];
            traced_buses_set.insert(internal_idx_to_bus_id[parent_idx]);
            for (const auto& conn : adjacency_list[current_idx]) {
                if (conn.internal_bus_idx == parent_idx) {
                    traced_branches_set.insert(conn.branch_id);
                    break;
                }
            }
            current_idx = parent_idx;
        }
        result.buses.assign(traced_buses_set.begin(), traced_buses_set.end());
        result.branches.assign(traced_branches_set.begin(), traced_branches_set.end());
    }

    // --- 步骤 3: 对结果进行排序，方便测试和比较 ---
    std::sort(result.buses.begin(), result.buses.end());
    std::sort(result.branches.begin(), result.branches.end());

    return result;
}

// --- 9. 断开支路 ---
bool PowerSystemTopology::openBranch(BranchId branch_id_to_open)
{
    auto it = branch_endpoints_map.find(branch_id_to_open);
    if (it == branch_endpoints_map.end())
        return false;

    BusId bus1_id = it->second.first;
    BusId bus2_id = it->second.second;
    int u_idx = getBusInternalIndex(bus1_id);
    int v_idx = getBusInternalIndex(bus2_id);

    if (u_idx == -1 || v_idx == -1)
        return false;

    auto& u_conns = adjacency_list[u_idx];
    u_conns.erase(
        std::remove_if(u_conns.begin(), u_conns.end(),
            [v_idx, branch_id_to_open](const AdjacencyInfo& conn) {
                return conn.internal_bus_idx == v_idx && conn.branch_id == branch_id_to_open;
            }),
        u_conns.end());

    auto& v_conns = adjacency_list[v_idx];
    v_conns.erase(
        std::remove_if(v_conns.begin(), v_conns.end(),
            [u_idx, branch_id_to_open](const AdjacencyInfo& conn) {
                return conn.internal_bus_idx == u_idx && conn.branch_id == branch_id_to_open;
            }),
        v_conns.end());

    branch_endpoints_map.erase(it);
    return true;
}