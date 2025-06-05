// traditional_threaded_sim.cpp
// 一个简化的基于传统多线程的VPP（虚拟电厂）频率响应仿真程序。
// 用于与基于协程的仿真方法进行性能对比。
#include <atomic> // 用于原子变量 (std::atomic)，实现线程安全的全局计数器等
#include <chrono> // 用于高精度时间测量和线程休眠 (std::chrono::high_resolution_clock, std::chrono::milliseconds)
#include <cmath> // 标准数学函数库 (std::abs, std::sin, std::cos, std::exp)
#include <condition_variable> // 用于线程间的条件同步 (std::condition_variable)
#include <fstream> // 用于文件输入输出 (std::ofstream，记录仿真数据)
#include <iomanip> // 用于输出格式化 (std::fixed, std::setprecision)
#include <iostream> // 标准输入输出流 (std::cout)
#include <mutex> // 用于互斥锁 (std::mutex, std::lock_guard, std::unique_lock)，保护共享数据
#include <random> // 用于生成随机数 (std::random_device, std::mt19937, std::uniform_real_distribution)
#include <thread> // C++标准线程库 (std::thread)
#include <vector> // C++标准动态数组 (std::vector，存储线程对象)

// 平台相关的头文件，用于内存统计
#include <sys/resource.h> // Linux平台下用于 getrusage 获取资源使用情况
#include <unistd.h> // Linux平台下的POSIX API (此处可能间接需要，如 sysconf)

// --- 仿真参数定义 ---
const int NUM_EV_STATIONS = 10; // 模拟的电动汽车充电站数量
const int PILES_PER_STATION = 5; // 每个充电站包含的充电桩数量
const int NUM_ESS_UNITS = 2; // 模拟的分布式储能单元数量
const double SIMULATION_DURATION_SECONDS = 10.0; // 仿真总时长（模拟的秒数）
const double FREQUENCY_UPDATE_INTERVAL_MS = 20.0; // 频率信息更新的时间间隔（毫秒），也即设备线程的响应周期/唤醒周期
const double DISTURBANCE_START_TIME_S = 1.0; // 频率扰动开始的仿真时间点（秒）

// --- 共享频率数据结构 ---
// 用于在频率预言机线程和设备线程之间安全地传递当前频率偏差和仿真时间。
struct SharedFrequencyData {
    double current_freq_deviation_hz = 0.0; // 当前的系统频率偏差 (Hz)
    std::mutex mtx; // 互斥锁，用于保护此结构体中共享数据的并发访问
    std::condition_variable cv; // 条件变量，用于当频率数据更新时通知等待的设备线程
    long long current_sim_time_ms = 0; // 当前的仿真时间 (毫秒)，由预言机线程更新
};

// --- 设备类型枚举 ---
enum class DeviceType { EV_PILE, // 电动汽车充电桩
    ESS_UNIT // 储能单元
};

// --- 设备配置结构体 ---
// 存储每个参与调频的设备的静态配置参数。
struct DeviceConfig {
    DeviceType type; // 设备类型
    double base_power_kW; // 基准功率 (kW)
    double gain_kW_per_Hz; // 调节增益 (kW/Hz)
    double deadband_Hz; // 频率死区 (Hz)
    double max_output_kW; // 最大输出功率 (kW)
    double min_output_kW; // 最小输出功率 (kW, 通常为负表示充电)
    double soc_min_threshold; // SOC最小允许阈值 (0.0-1.0)
    double soc_max_threshold; // SOC最大允许阈值 (0.0-1.0)
    double battery_capacity_kWh; // 电池容量 (kWh)，用于SOC更新
};

// --- 设备动态状态结构体 ---
// 存储每个设备在仿真过程中的动态状态。
struct DeviceState {
    double current_power_kW = 0.0; // 设备当前的实际功率 (kW)
    double soc = 0.5; // 设备的荷电状态 (State of Charge, 0.0-1.0)
};

// --- 全局共享变量 ---
std::atomic<double> g_total_vpp_power_kw(0.0); // VPP聚合的总功率 (kW)，使用原子类型以支持线程安全更新
std::atomic<bool> g_simulation_running(true); // 标志仿真是否仍在运行，用于控制线程的生命周期
std::ofstream g_data_logger("traditional_threaded_vpp_results.csv"); // 数据记录文件流

// --- 内存统计函数 (传统线程版) ---
// 获取当前进程的峰值内存使用量 (KB)。
long get_peak_memory_usage_kb_traditional()
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // 在Linux上，ru_maxrss 通常以KB为单位报告峰值驻留集大小。
        return usage.ru_maxrss;
    }
    std::cerr << "错误: 无法通过getrusage获取峰值内存使用情况。" << std::endl;
    return -1; // 获取失败返回-1
}

// --- 频率计算函数系数 (与HECS项目中保持一致，但重命名以避免链接冲突) ---
const double P_f_coeff_fs_trad = 0.0862;
const double M_f_coeff_fs_trad = 0.1404;
const double M1_f_coeff_fs_trad = 0.1577;
const double M2_f_coeff_fs_trad = 0.0397;
const double N_f_coeff_fs_trad = 0.125;

// 根据相对扰动时间计算频率偏差 (传统线程版，函数体与HECS版相同)
double calculate_frequency_deviation_traditional(double t_relative)
{
    if (t_relative < 0)
        return 0.0;
    // 频率偏差计算公式 (与HECS版本一致)
    double f_dev = -(M_f_coeff_fs_trad + (M1_f_coeff_fs_trad * std::sin(M_f_coeff_fs_trad * t_relative) - M_f_coeff_fs_trad * std::cos(M_f_coeff_fs_trad * t_relative)))
        / M2_f_coeff_fs_trad * std::exp(-N_f_coeff_fs_trad * t_relative) * P_f_coeff_fs_trad;
    return f_dev;
}

// --- 设备线程的主函数 ---
// 每个参与VPP调频的设备（EV充电桩或ESS单元）都由此函数在单独的线程中运行。
// device_id: 设备的唯一标识符 (主要用于日志或调试)。
// config: 此设备的配置参数。
// freq_data: 对共享频率数据的引用，用于获取最新的频率偏差和仿真时间。
void device_thread_func(int device_id, DeviceConfig config, SharedFrequencyData& freq_data)
{
    DeviceState state; // 设备自身的动态状态

    // 初始化SOC：为每个设备线程使用不同的随机种子，以产生多样化的初始SOC值。
    std::random_device rd;
    std::mt19937 gen(rd() ^ (device_id + static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()))); // 混合种子增加随机性
    std::uniform_real_distribution<> distrib_soc(0.3, 0.8); // SOC在30%到80%之间均匀分布
    state.soc = distrib_soc(gen);

    // 初始化设备功率为基准功率，并更新VPP总功率
    state.current_power_kW = config.base_power_kW;
    g_total_vpp_power_kw.fetch_add(state.current_power_kW, std::memory_order_relaxed); // 原子性加法

    long long last_update_sim_time_ms = 0; // 记录本设备上次处理的仿真时间戳

    // 线程主循环，只要仿真仍在运行
    while (g_simulation_running.load(std::memory_order_acquire)) {
        double freq_dev_to_use; // 本次计算使用的频率偏差
        long long current_sim_time_ms_local; // 本次计算对应的仿真时间

        // --- 等待新的频率数据 ---
        {
            std::unique_lock<std::mutex> lock(freq_data.mtx); // 获取共享数据的互斥锁
            // 等待条件变量：
            // 1. 直到共享的仿真时间 (freq_data.current_sim_time_ms) 前进到比本设备上次处理的时间更新；
            // 2. 或者，直到仿真结束标志 (g_simulation_running) 被设为false。
            freq_data.cv.wait(lock, [&]() {
                return freq_data.current_sim_time_ms > last_update_sim_time_ms || !g_simulation_running.load(std::memory_order_relaxed);
            });

            if (!g_simulation_running.load(std::memory_order_relaxed)) { // 如果是因仿真结束而被唤醒，则退出循环
                break;
            }
            // 读取共享的频率偏差和仿真时间
            freq_dev_to_use = freq_data.current_freq_deviation_hz;
            current_sim_time_ms_local = freq_data.current_sim_time_ms;
        } // 锁在此处自动释放

        // --- 计算设备功率响应 (逻辑与HECS版本中的VPP控制器基本一致) ---
        double old_power_kw = state.current_power_kW; // 保存旧功率，用于计算总功率变化
        double new_calculated_power_kW = config.base_power_kW; // 从基准功率开始
        double abs_freq_dev = std::abs(freq_dev_to_use);

        if (abs_freq_dev > config.deadband_Hz) { // 如果频率偏差超出死区
            if (freq_dev_to_use < 0) { // 频率下降 (欠频)
                double effective_df_drop = freq_dev_to_use + config.deadband_Hz; // 有效偏差 (负值)
                if (config.type == DeviceType::EV_PILE) { // EV充电桩
                    if (state.soc >= config.soc_min_threshold) { // SOC允许
                        new_calculated_power_kW = -config.gain_kW_per_Hz * effective_df_drop; // 增加出力/减少充电
                    } else { // SOC过低
                        if (config.base_power_kW < 0) // 如果原计划充电
                            new_calculated_power_kW = 0.0; // 则停止充电
                        // else 保持base_power_kW (可能为0或正)
                    }
                } else { // ESS单元
                    new_calculated_power_kW = -config.gain_kW_per_Hz * effective_df_drop; // 增加出力
                }
            } else { // 频率上升 (过频)
                double effective_df_rise = freq_dev_to_use - config.deadband_Hz; // 有效偏差 (正值)
                double power_change = -config.gain_kW_per_Hz * effective_df_rise; // 功率变化量 (应为负)
                new_calculated_power_kW = config.base_power_kW + power_change; // 减少出力/增加充电
            }
        }

        // 应用功率上下限约束
        new_calculated_power_kW = std::max(config.min_output_kW, std::min(config.max_output_kW, new_calculated_power_kW));

        // 对EV充电桩应用额外的SOC约束 (防止过充/过放)
        if (config.type == DeviceType::EV_PILE) {
            if (new_calculated_power_kW < 0 && state.soc >= config.soc_max_threshold) // 欲充电但SOC已满
                new_calculated_power_kW = 0.0; // 停止充电
            if (new_calculated_power_kW > 0 && state.soc <= config.soc_min_threshold) // 欲放电但SOC已空
                new_calculated_power_kW = 0.0; // 停止放电
        }

        // 更新VPP总功率 (原子操作)
        g_total_vpp_power_kw.fetch_add((new_calculated_power_kW - old_power_kw), std::memory_order_relaxed);
        state.current_power_kW = new_calculated_power_kW; // 更新设备自身功率状态

        // --- 更新设备SOC ---
        // 时间间隔 dt 固定为频率预言机的更新周期
        double dt_seconds = FREQUENCY_UPDATE_INTERVAL_MS / 1000.0;
        double dt_hours = dt_seconds / 3600.0;
        // 能量变化 (kWh) = 功率 (kW) * 时间 (h)
        // 注意: P > 0 表示放电 (SOC减少)，P < 0 表示充电 (SOC增加)。
        // 因此，SOC变化量 = - 能量变化量 / 电池容量
        double energy_change_kWh = state.current_power_kW * dt_hours;
        if (config.battery_capacity_kWh > 1e-6) { // 避免除以零
            state.soc -= (energy_change_kWh / config.battery_capacity_kWh);
        }
        // 确保SOC在[0, 1]范围内
        state.soc = std::max(0.0, std::min(1.0, state.soc));

        // 更新本设备处理到的最新仿真时间戳
        last_update_sim_time_ms = current_sim_time_ms_local;
    } // 线程主循环结束

    // 线程即将退出前，从VPP总功率中减去本设备最后一次贡献的功率
    g_total_vpp_power_kw.fetch_sub(state.current_power_kW, std::memory_order_relaxed);
    // (可选) 可以在此记录设备线程退出的信息
    // std::cout << "设备线程 " << device_id << " 正在退出。" << std::endl;
}

// --- 频率预言机线程的主函数 ---
// 该线程负责模拟系统频率的变化，并周期性地更新共享频率数据，同时记录仿真日志。
void frequency_oracle_thread_func(SharedFrequencyData& freq_data)
{
    long long current_sim_time_ms = 0; // 预言机内部维护的当前仿真时间 (毫秒)

    // 写入数据日志文件的表头
    g_data_logger << "# SimTime_ms\tSimTime_s\tRelativeTime_s\tFreqDeviation_Hz\tTotalVppPower_kW\n";

    // 预言机主循环，只要仿真仍在运行
    while (g_simulation_running.load(std::memory_order_acquire)) {
        // 将当前仿真时间从毫秒转换为秒
        double sim_time_s = static_cast<double>(current_sim_time_ms) / 1000.0;
        // 计算相对于扰动开始时间的相对时间
        double relative_time_s = sim_time_s - DISTURBANCE_START_TIME_S;
        // 根据相对时间计算当前的理论频率偏差
        double freq_dev = calculate_frequency_deviation_traditional(relative_time_s);

        // --- 更新共享频率数据 ---
        {
            std::lock_guard<std::mutex> lock(freq_data.mtx); // 获取互斥锁
            freq_data.current_freq_deviation_hz = freq_dev; // 更新频率偏差
            freq_data.current_sim_time_ms = current_sim_time_ms; // 更新仿真时间
        } // 锁在此处自动释放
        freq_data.cv.notify_all(); // 通知所有等待此条件变量的设备线程数据已更新

        // --- 记录当前时刻的仿真数据到文件 ---
        g_data_logger << current_sim_time_ms << "\t"
                      << std::fixed << std::setprecision(3) << sim_time_s << "\t"
                      << std::fixed << std::setprecision(3) << relative_time_s << "\t"
                      << std::fixed << std::setprecision(5) << freq_dev << "\t"
                      << std::fixed << std::setprecision(2) << g_total_vpp_power_kw.load(std::memory_order_relaxed) << "\n";

        // --- 检查是否达到仿真结束时间 ---
        if (sim_time_s >= SIMULATION_DURATION_SECONDS) {
            g_simulation_running.store(false, std::memory_order_release); // 设置仿真结束标志
            freq_data.cv.notify_all(); // 再次通知，确保所有线程能感知到结束状态
            break; // 退出预言机循环
        }

        // --- 预言机线程休眠，以模拟仿真时间的推进 ---
        // 休眠时间等于频率更新的间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(FREQUENCY_UPDATE_INTERVAL_MS)));
        // 推进预言机内部的仿真时间
        current_sim_time_ms += static_cast<long long>(FREQUENCY_UPDATE_INTERVAL_MS);
    }

    // 确保仿真结束标志最终被设置，并通知所有线程（以防万一有线程错过了之前的通知）
    if (g_simulation_running.load(std::memory_order_relaxed)) { // 如果标志还未被设为false
        g_simulation_running.store(false, std::memory_order_release);
    }
    freq_data.cv.notify_all(); // 最后一次通知
    // std::cout << "[预言机线程] 仿真结束，已通知所有设备线程。" << std::endl;
}

// --- 主函数 ---
int main()
{
    std::cout << "--- 简化版传统多线程VPP仿真 (带性能统计) ---" << std::endl;
    int total_devices = NUM_EV_STATIONS * PILES_PER_STATION + NUM_ESS_UNITS;
    std::cout << "警告: 即将创建 " << total_devices << " 个设备线程。这可能非常耗时且占用大量系统资源。" << std::endl;

    // 记录仿真开始时的真实物理时间
    auto real_time_sim_start = std::chrono::high_resolution_clock::now();

    SharedFrequencyData shared_freq_data; // 创建共享频率数据实例
    std::vector<std::thread> device_threads; // 用于存储所有设备线程对象的向量

    // 创建并启动频率预言机线程
    std::thread oracle_thread(frequency_oracle_thread_func, std::ref(shared_freq_data));

    int device_id_counter = 0; // 用于为设备分配唯一ID

    // 创建并启动EV充电桩的设备线程
    for (int i = 0; i < NUM_EV_STATIONS; ++i) {
        for (int j = 0; j < PILES_PER_STATION; ++j) {
            DeviceConfig ev_config;
            ev_config.type = DeviceType::EV_PILE;
            // 示例：按一定规则设置不同的基准充电功率，使其与HECS版本中的EV初始化逻辑更接近
            int temp_id_for_base_power = device_id_counter;
            if (temp_id_for_base_power % 3 == 0)
                ev_config.base_power_kW = -5.0; // 5kW充电
            else if (temp_id_for_base_power % 3 == 1)
                ev_config.base_power_kW = -3.5; // 3.5kW充电
            else
                ev_config.base_power_kW = 0.0; // 不充电

            ev_config.gain_kW_per_Hz = 4.0;
            ev_config.deadband_Hz = 0.03;
            ev_config.max_output_kW = 5.0; // 最大放电功率 (V2G)
            ev_config.min_output_kW = -5.0; // 最大充电功率
            ev_config.soc_min_threshold = 0.10; // 与HECS版本一致
            ev_config.soc_max_threshold = 0.95; // 与HECS版本一致
            ev_config.battery_capacity_kWh = 50.0; // 典型EV电池容量

            // 创建并启动线程，将配置和共享数据传递给线程函数
            device_threads.emplace_back(device_thread_func, device_id_counter++, ev_config, std::ref(shared_freq_data));
        }
    }

    // 创建并启动ESS单元的设备线程
    for (int i = 0; i < NUM_ESS_UNITS; ++i) {
        DeviceConfig ess_config;
        ess_config.type = DeviceType::ESS_UNIT;
        ess_config.base_power_kW = 0.0; // ESS平时待机
        // ess_config.gain_kW_per_Hz = 1000.0 / (0.03 * 50.0); // 这个50.0是什么？额定频率？
        // 如果gain定义为kW/Hz，那么假设在0.03Hz偏差时响应1000kW，则gain = 1000/0.03
        ess_config.gain_kW_per_Hz = 1000.0 / 0.03;
        ess_config.deadband_Hz = 0.03;
        ess_config.max_output_kW = 1000.0;
        ess_config.min_output_kW = -1000.0;
        ess_config.soc_min_threshold = 0.05; // 与HECS版本一致
        ess_config.soc_max_threshold = 0.95; // 与HECS版本一致
        ess_config.battery_capacity_kWh = 2000.0; // 大型ESS单元容量

        device_threads.emplace_back(device_thread_func, device_id_counter++, ess_config, std::ref(shared_freq_data));
    }

    std::cout << "已成功启动 " << device_threads.size() << " 个设备线程。" << std::endl;
    std::cout << "仿真将运行 " << SIMULATION_DURATION_SECONDS << " 秒 (模拟时间)..." << std::endl;

    // 等待频率预言机线程结束 (它会在仿真时间到达或被外部停止时结束)
    if (oracle_thread.joinable()) {
        oracle_thread.join();
    }
    // std::cout << "频率预言机线程已成功汇合 (joined)。" << std::endl;

    // 等待所有设备线程结束
    for (auto& th : device_threads) {
        if (th.joinable()) {
            th.join();
        }
    }
    // std::cout << "所有设备线程已成功汇合 (joined)。" << std::endl;

    // 关闭数据记录文件流
    if (g_data_logger.is_open()) {
        g_data_logger.close();
    }

    // 记录仿真结束时的真实物理时间，并计算总耗时
    auto real_time_sim_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> real_time_elapsed_seconds = real_time_sim_end - real_time_sim_start;

    // --- 输出仿真结果统计 ---
    std::cout << "\n--- 传统多线程仿真已结束 --- " << std::endl;
    std::cout << "模拟的总时长: " << SIMULATION_DURATION_SECONDS << " 秒。" << std::endl;
    std::cout << "真实物理执行耗时: " << std::fixed << std::setprecision(3) << real_time_elapsed_seconds.count() << " 秒。" << std::endl;

    // 获取并打印峰值内存使用情况
    long peak_mem_kb = get_peak_memory_usage_kb_traditional();
    if (peak_mem_kb != -1) {
        std::cout << "峰值内存使用 (近似值): " << peak_mem_kb << " KB (约 "
                  << std::fixed << std::setprecision(2) << peak_mem_kb / 1024.0 << " MB)。" << std::endl;
    } else {
        std::cout << "未能获取峰值内存使用数据。" << std::endl;
    }

    std::cout << "仿真结果已保存到文件: traditional_threaded_vpp_results.csv" << std::endl;

    return 0;
}