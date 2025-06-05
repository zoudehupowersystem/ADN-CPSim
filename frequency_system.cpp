// frequency_system.cpp
// 实现了与频率响应相关的组件构造函数和协程任务逻辑。
#include "frequency_system.h"
#include "logging_utils.h" // 引入日志工具，用于 g_console_logger, g_data_file_logger
#include <chrono> // C++时间库，用于获取仿真时间
#include <cmath> // 标准数学函数库，用于 std::abs, std::sin, std::cos, std::exp 等
#include <iomanip> // 用于输出格式化，如 std::fixed, std::setprecision (虽然主要通过spdlog格式化)

// 全局调度器指针 - 任务可能需要访问它以获取当前仿真时间或触发事件 (如果未通过参数显式传递)
// 注意：此全局变量应在 main.cpp 中定义和初始化。在大型项目中，推荐使用依赖注入。
extern cps_coro::Scheduler* g_scheduler;

// PhysicalStateComponent 构造函数实现
PhysicalStateComponent::PhysicalStateComponent(double power, double s)
    : current_power_kW(power) // 初始化当前功率
    , soc(s) // 初始化荷电状态 (SOC)
{
    // 构造函数体，可添加其他初始化逻辑 (如果需要)
}

// FrequencyControlConfigComponent 构造函数实现
FrequencyControlConfigComponent::FrequencyControlConfigComponent(
    DeviceType t, double base_p, double gain, double db,
    double max_p, double min_p, double soc_min, double soc_max)
    : type(t) // 初始化设备类型
    , base_power_kW(base_p) // 初始化基准功率
    , gain_kW_per_Hz(gain) // 初始化调节增益
    , deadband_Hz(db) // 初始化频率死区
    , max_output_kW(max_p) // 初始化最大输出功率
    , min_output_kW(min_p) // 初始化最小输出功率
    , soc_min_threshold(soc_min) // 初始化SOC最小阈值
    , soc_max_threshold(soc_max) // 初始化SOC最大阈值
{
    // 构造函数体，可添加其他初始化逻辑 (如果需要)
}

// 频率计算模型的示例系数 (这些系数仅为演示，实际模型会更复杂)
const double P_f_coeff_fs = 0.0862; // 功率-频率特性相关系数
const double M_f_coeff_fs = 0.1404; // 模型动态参数 M
const double M1_f_coeff_fs = 0.1577; // 模型动态参数 M1
const double M2_f_coeff_fs = 0.0397; // 模型动态参数 M2
const double N_f_coeff_fs = 0.125; // 模型动态参数 N (衰减因子相关)

// calculate_frequency_deviation 函数实现
// 根据一个简化的数学模型，基于扰动后的相对时间计算频率偏差。
double calculate_frequency_deviation(double t_relative)
{
    if (t_relative < 0) { // 如果在扰动发生之前，频率偏差为0
        return 0.0;
    }
    // 这是一个示例性的频率偏差计算公式，模拟扰动（如功率不平衡ΔP）后的系统频率动态响应。
    // 公式的具体形式取决于所采用的电力系统模型。
    double f_dev = -(M_f_coeff_fs + (M1_f_coeff_fs * std::sin(M_f_coeff_fs * t_relative) - M_f_coeff_fs * std::cos(M_f_coeff_fs * t_relative)))
        / M2_f_coeff_fs * std::exp(-N_f_coeff_fs * t_relative) * P_f_coeff_fs;
    return f_dev; // 返回计算得到的频率偏差 (Hz)
}

// frequencyOracleTask 协程任务实现
// 模拟频率预言机，周期性地计算并发布系统频率信息。
cps_coro::Task frequencyOracleTask(Registry& registry,
    const std::vector<Entity>& ev_entities,
    const std::vector<Entity>& ess_entities,
    double disturbance_start_time_s, // 扰动开始的仿真时间 (秒)
    double simulation_step_ms) // 预言机更新和发布事件的时间步长 (毫秒)
{
    // 使用控制台日志记录任务启动信息
    if (g_console_logger) {
        // 获取当前仿真时间用于日志时间戳 (如果 g_scheduler 有效)
        double current_time_for_log = g_scheduler ? g_scheduler->now().time_since_epoch().count() / 1.0 : 0.0;
        g_console_logger->info("[{:.1f}毫秒] [频率预言机] 任务已激活。扰动将于仿真时间 {:.1f}秒开始。更新步长: {}毫秒。",
            current_time_for_log,
            disturbance_start_time_s, simulation_step_ms);
    }

    // 如果数据文件日志记录器可用，则写入CSV文件头（列名）
    if (g_data_file_logger) {
        // 定义数据文件的列标题，使用制表符分隔 (TSV)，便于导入Excel或Pandas等工具
        g_data_file_logger->info("仿真时间_毫秒\t仿真时间_秒\t相对扰动时间_秒\t频率偏差_赫兹\tVPP总功率_千瓦");
    }

    while (true) { // 无限循环，模拟持续的频率信息发布
        // 协程等待 (挂起)，直到指定的仿真步长时间过去
        co_await cps_coro::delay(cps_coro::Scheduler::duration(static_cast<long long>(simulation_step_ms)));

        // 获取当前的仿真时间
        double current_sim_time_ms = g_scheduler ? g_scheduler->now().time_since_epoch().count() : 0.0;
        double current_sim_time_s = current_sim_time_ms / 1000.0; // 转换为秒

        // 计算相对于扰动开始时刻的相对时间
        double relative_time_s = current_sim_time_s - disturbance_start_time_s;

        // 根据相对时间计算当前的频率偏差
        double freq_dev_hz = calculate_frequency_deviation(relative_time_s);

        // 填充频率信息结构体
        FrequencyInfo freq_info;
        freq_info.current_sim_time_seconds = current_sim_time_s;
        freq_info.freq_deviation_hz = freq_dev_hz;

        // 如果调度器有效，触发频率更新事件，将最新的频率信息广播给其他协程
        if (g_scheduler) {
            g_scheduler->trigger_event(FREQUENCY_UPDATE_EVENT, freq_info);
        }

        // (可选) 计算并记录当前VPP（EV充电桩和ESS单元）的总功率输出
        double total_vpp_power_kw = 0;
        // 累加所有EV充电桩的功率
        for (Entity entity_id : ev_entities) {
            if (auto state = registry.get<PhysicalStateComponent>(entity_id)) { // 安全地获取组件
                total_vpp_power_kw += state->current_power_kW;
            }
        }
        // 累加所有ESS单元的功率
        for (Entity entity_id : ess_entities) {
            if (auto state = registry.get<PhysicalStateComponent>(entity_id)) { // 安全地获取组件
                total_vpp_power_kw += state->current_power_kW;
            }
        }

        // 将当前时刻的仿真状态（时间、频率偏差、总功率）记录到数据文件
        if (g_data_file_logger) {
            g_data_file_logger->info("{:.0f}\t{:.3f}\t{:.3f}\t{:.5f}\t{:.2f}", // 格式化输出
                current_sim_time_ms,
                current_sim_time_s,
                relative_time_s,
                freq_dev_hz,
                total_vpp_power_kw);
        }
    } // 循环继续，等待下一个仿真步长
}

// vppFrequencyResponseTask 协程任务实现
// 模拟VPP（虚拟电厂）中的设备根据接收到的频率偏差信息调整其功率输出。
cps_coro::Task vppFrequencyResponseTask(Registry& registry,
    const std::string& vpp_name, // VPP的名称 (用于日志输出)
    const std::vector<Entity>& managed_entities, // 此VPP实例管理的设备实体列表
    double /*simulation_step_ms_parameter*/) // 此参数在当前事件驱动模型下不直接用于SOC的dt计算
{
    if (g_console_logger) {
        double current_time_for_log = g_scheduler ? g_scheduler->now().time_since_epoch().count() / 1.0 : 0.0;
        g_console_logger->info("[{:.1f}毫秒] [VPP-{}] 任务已激活，采用事件驱动更新模式。正在等待 FREQUENCY_UPDATE_EVENT 事件。",
            current_time_for_log, vpp_name);
    }

    double last_processed_event_time_s = -1.0; // 上次成功处理的频率事件的仿真时间 (秒)，用于避免重复处理旧事件或乱序事件
    double vpp_instance_last_full_update_time_s = -1.0; // VPP内所有设备上次执行完整状态更新（包括SOC和功率计算）的仿真时间 (秒)
    double vpp_instance_last_full_update_freq_dev_hz = 0.0; // 上次完整更新时的系统频率偏差 (Hz)

    // 定义触发VPP内部设备状态更新的判断阈值
    const double FREQUENCY_CHANGE_THRESHOLD_HZ = 0.01; // 频率偏差变化阈值 (Hz)。当新频率与上次更新时频率的差值超过此阈值，触发更新。
    const double TIME_THRESHOLD_SECONDS = 1.0; // 时间间隔阈值 (秒)。即使频率变化不大，如果距离上次更新的时间超过此阈值，也强制触发更新。

    while (true) { // 无限循环，持续监听和响应频率事件
        // 协程等待 (挂起)，直到 FREQUENCY_UPDATE_EVENT 事件被触发，并获取事件数据 (FrequencyInfo)
        FrequencyInfo current_freq_info = co_await cps_coro::wait_for_event<FrequencyInfo>(FREQUENCY_UPDATE_EVENT);

        // 检查收到的事件是否是过时的 (即其仿真时间早于或等于上次已处理的事件时间)
        if (current_freq_info.current_sim_time_seconds <= last_processed_event_time_s) {
            // 如果是旧事件或时间戳相同的重复事件，则跳过处理，继续等待新事件
            continue;
        }
        last_processed_event_time_s = current_freq_info.current_sim_time_seconds; // 更新上次成功处理的事件时间

        bool perform_full_update = false; // 标志位，指示本轮是否需要对VPP内所有设备执行完整状态更新
        double dt_since_last_full_update = 0.0; // 距离上次完整更新的时间间隔 (秒)

        // 判断是否需要执行完整更新
        if (vpp_instance_last_full_update_time_s < 0) { // 如果是VPP任务启动后的第一次频率事件
            perform_full_update = true; // 必须执行完整更新
        } else {
            // 计算当前事件时间与上次完整更新时间之间的时间差
            dt_since_last_full_update = current_freq_info.current_sim_time_seconds - vpp_instance_last_full_update_time_s;
            if (dt_since_last_full_update < 0) { // 安全检查，防止仿真时间回溯导致负的dt
                dt_since_last_full_update = 0;
            }

            // 计算当前频率偏差与上次更新时频率偏差的绝对差值
            double freq_diff_abs = std::abs(current_freq_info.freq_deviation_hz - vpp_instance_last_full_update_freq_dev_hz);

            // 条件1: 如果频率变化的绝对值超过了设定的阈值
            if (freq_diff_abs > FREQUENCY_CHANGE_THRESHOLD_HZ) {
                perform_full_update = true;
            }
            // 条件2: 如果距离上次完整更新的时间间隔超过了设定的时间阈值
            if (dt_since_last_full_update >= TIME_THRESHOLD_SECONDS) {
                perform_full_update = true;
            }
        }

        if (perform_full_update) { // 如果满足任一更新条件
            // // 调试日志示例 (可以取消注释以查看详细更新过程)
            // if(g_console_logger) {
            //     double current_time_for_log = g_scheduler ? g_scheduler->now().time_since_epoch().count() / 1.0 : 0.0;
            //     g_console_logger->debug(
            //         "[{:.1f}毫秒] [VPP-{}] 满足更新条件。执行设备状态完整更新。当前仿真时间: {:.3f}秒。频率偏差: {:.5f}赫兹。距上次完整更新间隔: {:.3f}秒。",
            //         current_time_for_log, vpp_name, current_freq_info.current_sim_time_seconds,
            //         current_freq_info.freq_deviation_hz, dt_since_last_full_update);
            // }

            // 遍历VPP管理的所有设备实体
            for (Entity entity_id : managed_entities) {
                auto config = registry.get<FrequencyControlConfigComponent>(entity_id); // 获取设备的频率控制配置组件
                auto state = registry.get<PhysicalStateComponent>(entity_id); // 获取设备的物理状态组件

                if (!config || !state) { // 如果任一组件不存在，则跳过此设备
                    continue;
                }

                // 更新SOC状态 (如果不是第一次更新且时间间隔有效)
                // SOC(t) = SOC(t-dt) - P(t-dt) * dt / Capacity
                if (vpp_instance_last_full_update_time_s >= 0 && dt_since_last_full_update > 1e-6) { // 避免dt为零或极小值
                    double power_during_last_interval_kW = state->current_power_kW; // 上个时间间隔内的平均功率 (kW)
                    // 计算能量变化量 (kWh) = 功率 (kW) * 时间间隔 (小时)
                    double energy_change_kWh = power_during_last_interval_kW * (dt_since_last_full_update / 3600.0);

                    // 根据设备类型和假设的典型电池容量更新SOC
                    // 注意：P>0表示放电（SOC减少），P<0表示充电（SOC增加）。能量变化与SOC变化符号相反。
                    if (config->type == FrequencyControlConfigComponent::DeviceType::EV_PILE) {
                        double typical_ev_battery_capacity_kWh = 50.0; // 假设EV电池典型容量 (kWh)
                        if (typical_ev_battery_capacity_kWh > 0) {
                            state->soc -= (energy_change_kWh / typical_ev_battery_capacity_kWh);
                        }
                    } else if (config->type == FrequencyControlConfigComponent::DeviceType::ESS_UNIT) {
                        double typical_ess_capacity_kWh = 2000.0; // 假设ESS单元典型容量 (kWh)
                        if (typical_ess_capacity_kWh > 0) {
                            state->soc -= (energy_change_kWh / typical_ess_capacity_kWh);
                        }
                    }
                    // 确保SOC值在 [0.0, 1.0] 的有效范围内
                    state->soc = std::max(0.0, std::min(1.0, state->soc));
                }

                // 根据当前频率偏差计算新的目标功率 (一次调频逻辑)
                double new_calculated_power_kW = config->base_power_kW; // 从基准功率开始计算
                double current_actual_freq_dev_hz = current_freq_info.freq_deviation_hz; // 当前实际频率偏差 (Hz)
                double current_abs_actual_freq_dev_hz = std::abs(current_actual_freq_dev_hz); // 频率偏差的绝对值

                // 检查频率偏差是否超出了死区范围
                if (current_abs_actual_freq_dev_hz > config->deadband_Hz) {
                    if (current_actual_freq_dev_hz < 0) { // 频率下降 (欠频)，系统需要更多功率
                        // 计算有效的频率下降值 (已考虑死区，此值为负)
                        double effective_df_drop_hz = current_actual_freq_dev_hz + config->deadband_Hz;
                        // 对于欠频，设备应增加输出功率（放电）或减少消耗功率（减少充电）
                        // 功率变化 P_adj = -Gain * df_effective (Gain通常为正，df_effective为负，所以 P_adj 为正)
                        // new_calculated_power_kW = config->base_power_kW - config->gain_kW_per_Hz * effective_df_drop_hz;
                        // 这里假设 gain_kW_per_Hz 定义为功率变化量 / 频率偏差绝对值。
                        // 若频率下降，需要正的功率增量。若 gain 定义为 df<0 时 P 增加的比例，则 P_adj = gain * abs(effective_df_drop_hz)
                        // 采用 P_target = P_base - K * df (K为正，df为实际偏差)
                        // 若df<0(欠频)，则P_target = P_base - K*df (df<0 => P_target > P_base，增加出力)
                        // 若df>0(过频)，则P_target = P_base - K*df (df>0 => P_target < P_base，减少出力)
                        // 此处代码 `new_calculated_power_kW = -config->gain_kW_per_Hz * effective_df_drop_hz;`
                        // 意味着不考虑 base_power_kW，而是直接根据偏差和增益计算一个功率值。这可能是特定模型的实现。
                        // 假设 gain_kW_per_Hz 是正值。effective_df_drop_hz 是负值。结果是正功率（放电）。
                        // 这通常用于储能或发电机。对于负荷，逻辑可能相反或更复杂。

                        if (config->type == FrequencyControlConfigComponent::DeviceType::EV_PILE) {
                            // EV在欠频时：如果SOC允许，则尝试减少充电功率或反向放电 (若支持V2G)
                            if (state->soc >= config->soc_min_threshold) { // SOC高于最低阈值，允许放电或减少充电
                                // 假设EV的gain也表示增加“等效发电”的能力
                                new_calculated_power_kW = -config->gain_kW_per_Hz * effective_df_drop_hz; // 结果为正，表示放电或减少负荷
                            } else if (config->base_power_kW < 0) { // SOC过低，且原计划是充电状态
                                new_calculated_power_kW = 0.0; // 则尝试停止充电，减少系统负担
                            } // 其他情况 (如SOC过低且原计划不充电或放电) 保持基准功率 (可能为0或正)
                        } else if (config->type == FrequencyControlConfigComponent::DeviceType::ESS_UNIT) {
                            // ESS在欠频时：增加输出功率 (放电)
                            new_calculated_power_kW = -config->gain_kW_per_Hz * effective_df_drop_hz; // 结果为正
                        }
                    } else { // 频率上升 (过频)，系统需要减少功率注入或增加负荷
                        // 计算有效的频率上升值 (已考虑死区，此值为正)
                        double effective_df_rise_hz = current_actual_freq_dev_hz - config->deadband_Hz;
                        // 对于过频，设备应减少输出功率（减少发电）或增加消耗功率（增加充电）
                        // 功率变化 P_adj = -Gain * df_effective (Gain为正，df_effective为正，所以 P_adj 为负)
                        // new_calculated_power_kW = config->base_power_kW - config->gain_kW_per_Hz * effective_df_rise_hz;
                        double power_change_due_to_freq = -config->gain_kW_per_Hz * effective_df_rise_hz; // 功率变化量，应为负
                        new_calculated_power_kW = config->base_power_kW + power_change_due_to_freq; // 叠加到基准功率
                    }
                } // 如果频率偏差在死区内，new_calculated_power_kW 保持为 config->base_power_kW

                // 将计算得到的功率限制在设备的物理最大/最小输出能力范围内
                new_calculated_power_kW = std::max(config->min_output_kW, std::min(config->max_output_kW, new_calculated_power_kW));

                // 对EV充电桩应用额外的SOC约束，防止过充或过放
                if (config->type == FrequencyControlConfigComponent::DeviceType::EV_PILE) {
                    // 如果计算出的新功率是充电状态 (new_calculated_power_kW < 0)，但SOC已达到或超过上限
                    if (new_calculated_power_kW < 0 && state->soc >= config->soc_max_threshold) {
                        new_calculated_power_kW = 0.0; // 则停止充电，将功率设为0 (或根据策略设为最小充电)
                    }
                    // 如果计算出的新功率是放电状态 (new_calculated_power_kW > 0)，但SOC已达到或低于下限
                    if (new_calculated_power_kW > 0 && state->soc <= config->soc_min_threshold) {
                        new_calculated_power_kW = 0.0; // 则停止放电，将功率设为0
                    }
                }
                // 更新设备的当前实际功率状态
                state->current_power_kW = new_calculated_power_kW;
            } // 结束对VPP内所有设备的遍历和更新

            // 更新VPP实例级的上次完整更新时间和对应的频率偏差，用于下一轮判断
            vpp_instance_last_full_update_time_s = current_freq_info.current_sim_time_seconds;
            vpp_instance_last_full_update_freq_dev_hz = current_freq_info.freq_deviation_hz;
        } // 结束 perform_full_update 的条件块
    } // 循环继续，等待下一个频率事件
}