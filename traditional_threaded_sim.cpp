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
#include <sstream> // 用于日志中的设备名称
#include <thread> // C++标准线程库 (std::thread)
#include <vector> // C++标准动态数组 (std::vector，存储线程对象)

// 平台相关的头文件，用于内存统计
#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__) // macOS 也使用 getrusage
#include <sys/resource.h>
#include <unistd.h>
#endif

// --- 仿真参数定义 (与HECS版本对齐) ---
const int NUM_EV_STATIONS = 44; // 模拟的电动汽车充电站数量
const int PILES_PER_STATION = 10; // 每个充电站包含的充电桩数量
const int NUM_ESS_UNITS = 60; // 模拟的分布式储能单元数量
const double SIMULATION_DURATION_SECONDS = 70.0; // 仿真总时长（模拟的秒数）
const double FREQUENCY_UPDATE_INTERVAL_MS = 20.0; // 预言机发布频率的间隔
const double DISTURBANCE_START_TIME_S = 5.0; // 频率扰动开始的仿真时间点（秒）

// --- HECS版本 individualDeviceFrequencyResponseTask 中的更新判断阈值 ---
const double DEVICE_FREQUENCY_CHANGE_THRESHOLD_HZ = 0.005;
const double DEVICE_TIME_THRESHOLD_SECONDS = 0.5;

// --- 共享频率数据结构 ---
struct SharedFrequencyData {
    double current_freq_deviation_hz = 0.0;
    std::mutex mtx;
    std::condition_variable cv;
    long long current_sim_time_ms = 0; // 由预言机更新的“当前”仿真时间
};

// --- 设备类型枚举 ---
enum class DeviceType { EV_PILE,
    ESS_UNIT };

// --- 设备配置结构体 ---
struct DeviceConfig {
    int id; // 设备唯一ID，用于日志
    std::string log_name; // 用于日志的设备名
    DeviceType type;
    double base_power_kW;
    double gain_kW_per_Hz;
    double deadband_Hz;
    double max_output_kW;
    double min_output_kW;
    double soc_min_threshold;
    double soc_max_threshold;
    double battery_capacity_kWh; // 与HECS对齐
    double initial_soc; // 与HECS对齐
};

// --- 设备动态状态结构体 ---
struct DeviceState {
    double current_power_kW = 0.0;
    double soc = 0.5;
    // 每个设备线程内部维护，用于决定是否执行完整更新
    long long device_last_full_update_sim_time_ms = -1;
    double device_last_full_update_freq_dev_hz = 0.0;
};

// --- 全局共享变量 ---
std::atomic<double> g_total_vpp_power_kw(0.0);
std::atomic<bool> g_simulation_running(true);
std::ofstream g_data_logger("traditional_threaded_vpp_results.csv");

// --- 内存统计函数 ---
long get_peak_memory_usage_kb_traditional()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return static_cast<long>(pmc.PeakWorkingSetSize / 1024);
    }
    std::cerr << "错误: 在Windows上获取峰值内存使用信息失败，错误码: " << GetLastError() << std::endl;
    return -1;
#elif defined(__linux__) || defined(__APPLE__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        return usage.ru_maxrss / 1024; // macOS: bytes to KB
#else
        return usage.ru_maxrss; // Linux: already in KB
#endif
    }
    std::cerr << "错误: 无法通过getrusage获取峰值内存使用情况。" << std::endl;
    return -1;
#else
    std::cerr << "错误: 此平台不支持峰值内存使用统计。" << std::endl;
    return -1;
#endif
}

// --- 频率计算函数系数 ---
const double P_f_coeff_fs_trad = 0.0862;
const double M_f_coeff_fs_trad = 0.1404;
const double M1_f_coeff_fs_trad = 0.1577;
const double M2_f_coeff_fs_trad = 0.0397;
const double N_f_coeff_fs_trad = 0.125;

double calculate_frequency_deviation_traditional(double t_relative)
{
    if (t_relative < 0)
        return 0.0;
    double f_dev = -(M_f_coeff_fs_trad + (M1_f_coeff_fs_trad * std::sin(M_f_coeff_fs_trad * t_relative) - M_f_coeff_fs_trad * std::cos(M_f_coeff_fs_trad * t_relative)))
        / M2_f_coeff_fs_trad * std::exp(-N_f_coeff_fs_trad * t_relative) * P_f_coeff_fs_trad;
    return f_dev;
}

// --- 设备线程的主函数 ---
void device_thread_func(DeviceConfig config, SharedFrequencyData& freq_data)
{
    DeviceState state;
    state.soc = config.initial_soc;
    state.current_power_kW = config.base_power_kW;
    g_total_vpp_power_kw.fetch_add(state.current_power_kW, std::memory_order_relaxed);

    long long last_processed_event_sim_time_ms = 0; // 用于避免重复处理同一事件

    // std::cout << "调试: 设备线程 " << config.log_name << " (ID: " << config.id << ") 启动，初始SOC: " << state.soc << std::endl;

    while (g_simulation_running.load(std::memory_order_acquire)) {
        double current_event_freq_dev_hz;
        long long current_event_sim_time_ms;

        {
            std::unique_lock<std::mutex> lock(freq_data.mtx);
            freq_data.cv.wait(lock, [&]() {
                // 等待新的仿真时间步或仿真结束
                return freq_data.current_sim_time_ms > last_processed_event_sim_time_ms || !g_simulation_running.load(std::memory_order_relaxed);
            });

            if (!g_simulation_running.load(std::memory_order_relaxed)) {
                break;
            }
            current_event_freq_dev_hz = freq_data.current_freq_deviation_hz;
            current_event_sim_time_ms = freq_data.current_sim_time_ms;
        } // 锁释放

        // HECS版本会检查事件是否过时，我们也这样做
        if (current_event_sim_time_ms <= last_processed_event_sim_time_ms) {
            // std::cout << "调试: 设备 " << config.log_name << " 跳过已处理或过时事件 @ " << current_event_sim_time_ms << "ms" << std::endl;
            continue;
        }
        last_processed_event_sim_time_ms = current_event_sim_time_ms;

        // --- 判断是否需要执行完整更新 (逻辑同HECS individualDeviceFrequencyResponseTask) ---
        bool perform_update = false;
        double dt_since_last_full_update_seconds = 0.0;

        if (state.device_last_full_update_sim_time_ms < 0) { // 首次事件
            perform_update = true;
        } else {
            dt_since_last_full_update_seconds = static_cast<double>(current_event_sim_time_ms - state.device_last_full_update_sim_time_ms) / 1000.0;
            if (dt_since_last_full_update_seconds < 0)
                dt_since_last_full_update_seconds = 0; // 防御

            double freq_diff_abs = std::abs(current_event_freq_dev_hz - state.device_last_full_update_freq_dev_hz);

            if (freq_diff_abs > DEVICE_FREQUENCY_CHANGE_THRESHOLD_HZ) {
                perform_update = true;
            }
            if (dt_since_last_full_update_seconds >= DEVICE_TIME_THRESHOLD_SECONDS) {
                perform_update = true;
            }
        }

        if (perform_update) {
            // std::cout << "调试: 设备 " << config.log_name << " @ " << current_event_sim_time_ms << "ms 执行更新。偏差: " << current_event_freq_dev_hz << " Hz, 距上次更新: " << dt_since_last_full_update_seconds << "s" << std::endl;

            double old_power_kw = state.current_power_kW;

            // 1. 更新SOC状态 (基于上一个时间间隔的功率和正确的dt)
            // dt_for_soc_update_seconds 就是 dt_since_last_full_update_seconds
            if (state.device_last_full_update_sim_time_ms >= 0 && dt_since_last_full_update_seconds > 1e-6) {
                double energy_change_kWh = old_power_kw * (dt_since_last_full_update_seconds / 3600.0);
                if (config.battery_capacity_kWh > 1e-6) {
                    state.soc -= (energy_change_kWh / config.battery_capacity_kWh);
                }
                state.soc = std::max(0.0, std::min(1.0, state.soc));
            }

            // 2. 根据当前频率偏差计算新的目标功率
            double new_calculated_power_kW = config.base_power_kW;
            double abs_freq_dev = std::abs(current_event_freq_dev_hz);

            if (abs_freq_dev > config.deadband_Hz) {
                if (current_event_freq_dev_hz < 0) { // 欠频
                    double effective_df_drop = current_event_freq_dev_hz + config.deadband_Hz;
                    new_calculated_power_kW = -config.gain_kW_per_Hz * effective_df_drop; // HECS模型：直接计算响应量

                    if (config.type == DeviceType::EV_PILE) {
                        if (new_calculated_power_kW > 0 && state.soc < config.soc_min_threshold) { // 想放电但SOC低
                            new_calculated_power_kW = 0.0;
                        }
                        // 严格镜像HECS的这个else if (即使在欠频时new_calculated_power_kW < 0不常见)
                        else if (state.soc < config.soc_min_threshold && config.base_power_kW < 0 && new_calculated_power_kW < 0) {
                            new_calculated_power_kW = 0.0; // 停止充电
                        }
                    }
                } else { // 过频
                    double effective_df_rise = current_event_freq_dev_hz - config.deadband_Hz;
                    double power_change = -config.gain_kW_per_Hz * effective_df_rise;
                    new_calculated_power_kW = config.base_power_kW + power_change;
                }
            }

            // 3. 功率上下限约束
            new_calculated_power_kW = std::max(config.min_output_kW, std::min(config.max_output_kW, new_calculated_power_kW));

            // 4. EV的额外SOC约束
            if (config.type == DeviceType::EV_PILE) {
                if (new_calculated_power_kW < 0 && state.soc >= config.soc_max_threshold) { // 想充电但SOC满
                    new_calculated_power_kW = 0.0;
                }
                if (new_calculated_power_kW > 0 && state.soc <= config.soc_min_threshold) { // 想放电但SOC空 (再次确认)
                    new_calculated_power_kW = 0.0;
                }
            }

            // 5. 更新设备功率和全局总功率
            if (std::abs(new_calculated_power_kW - old_power_kw) > 1e-6) { // 只有功率实际变化时才更新
                g_total_vpp_power_kw.fetch_add((new_calculated_power_kW - old_power_kw), std::memory_order_relaxed);
                state.current_power_kW = new_calculated_power_kW;
            }

            // 更新此设备的上次完整更新时间和频率，用于下一轮判断
            state.device_last_full_update_sim_time_ms = current_event_sim_time_ms;
            state.device_last_full_update_freq_dev_hz = current_event_freq_dev_hz;
        } else {
            // std::cout << "调试: 设备 " << config.log_name << " @ " << current_event_sim_time_ms << "ms 未满足更新条件，跳过功率/SOC计算。" << std::endl;
        }
    } // 线程主循环结束

    // 线程退出前，从总功率中减去最后贡献的功率
    g_total_vpp_power_kw.fetch_sub(state.current_power_kW, std::memory_order_relaxed);
    // std::cout << "调试: 设备线程 " << config.log_name << " 退出。" << std::endl;
}

// --- 频率预言机线程的主函数 ---
void frequency_oracle_thread_func(SharedFrequencyData& freq_data)
{
    long long sim_time_ms_oracle = 0;

    g_data_logger << "# SimTime_ms\tSimTime_s\tRelativeTime_s\tFreqDeviation_Hz\tTotalVppPower_kW\n";

    while (g_simulation_running.load(std::memory_order_acquire)) {
        double sim_time_s = static_cast<double>(sim_time_ms_oracle) / 1000.0;
        double relative_time_s = sim_time_s - DISTURBANCE_START_TIME_S;
        double freq_dev = calculate_frequency_deviation_traditional(relative_time_s);

        {
            std::lock_guard<std::mutex> lock(freq_data.mtx);
            freq_data.current_freq_deviation_hz = freq_dev;
            freq_data.current_sim_time_ms = sim_time_ms_oracle; // 发布当前仿真时间
        }
        freq_data.cv.notify_all();

        // VPP总功率的记录：
        // HECS版本是在预言机中遍历所有设备当前状态求和。
        // 传统线程版g_total_vpp_power_kw是设备线程实时更新的原子变量。
        // 两者记录的TotalVppPower_kW含义略有不同：
        //  - HECS：记录的是上个major step后，各设备计算出的功率之和。
        //  - Traditional：记录的是当前时刻，已响应本次频率事件的设备更新后的功率之和（可能部分设备尚未完成处理）。
        // 为了更接近HECS的日志行为（记录一个相对稳定的、上一步的结果），可以考虑在预言机中引入微小延迟后读取，
        // 但这会增加复杂性。当前g_total_vpp_power_kw是动态更新的，更反映“实时”总和。
        // 暂时保持现状，因为核心是设备行为一致性。
        g_data_logger << sim_time_ms_oracle << "\t"
                      << std::fixed << std::setprecision(3) << sim_time_s << "\t"
                      << std::fixed << std::setprecision(3) << relative_time_s << "\t"
                      << std::fixed << std::setprecision(5) << freq_dev << "\t"
                      << std::fixed << std::setprecision(2) << g_total_vpp_power_kw.load(std::memory_order_relaxed) << "\n";
        g_data_logger.flush(); // 确保数据及时写入文件

        if (sim_time_s >= SIMULATION_DURATION_SECONDS) {
            g_simulation_running.store(false, std::memory_order_release);
            freq_data.cv.notify_all(); // 确保所有等待线程被唤醒以退出
            break;
        }

        // std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(FREQUENCY_UPDATE_INTERVAL_MS)));
        sim_time_ms_oracle += static_cast<long long>(FREQUENCY_UPDATE_INTERVAL_MS);
    }

    // 确保仿真结束标志最终被设置并通知
    if (g_simulation_running.load(std::memory_order_relaxed)) {
        g_simulation_running.store(false, std::memory_order_release);
    }
    freq_data.cv.notify_all();
    // std::cout << "调试: 频率预言机线程结束。" << std::endl;
}

// --- 主函数 ---
int main()
{
    std::cout << "--- 简化版传统多线程VPP仿真 (已尝试对齐HECS细粒度版) ---" << std::endl;
    int total_devices = NUM_EV_STATIONS * PILES_PER_STATION + NUM_ESS_UNITS;
    std::cout << "信息: 即将创建 " << total_devices << " 个设备线程。" << std::endl;

    auto real_time_sim_start = std::chrono::high_resolution_clock::now();

    SharedFrequencyData shared_freq_data;
    std::vector<std::thread> device_threads;

    std::thread oracle_thread(frequency_oracle_thread_func, std::ref(shared_freq_data));

    // 用于EV初始SOC的随机数生成器 (同HECS)
    std::random_device rd_ev_soc;
    std::mt19937 rng_ev_soc(rd_ev_soc());
    std::uniform_real_distribution<double> dist_ev_soc(0.25, 0.90);

    int device_id_counter = 0;

    for (int i = 0; i < NUM_EV_STATIONS; ++i) {
        for (int j = 0; j < PILES_PER_STATION; ++j) {
            DeviceConfig ev_config;
            ev_config.id = device_id_counter;
            std::ostringstream name_stream;
            name_stream << "EV桩_" << ev_config.id;
            ev_config.log_name = name_stream.str();

            ev_config.type = DeviceType::EV_PILE;
            if (device_id_counter % 3 == 0)
                ev_config.base_power_kW = -5.0;
            else if (device_id_counter % 3 == 1)
                ev_config.base_power_kW = -3.5;
            else
                ev_config.base_power_kW = 0.0;

            ev_config.gain_kW_per_Hz = 4.0;
            ev_config.deadband_Hz = 0.03;
            ev_config.max_output_kW = 5.0;
            ev_config.min_output_kW = -5.0;
            ev_config.soc_min_threshold = 0.10;
            ev_config.soc_max_threshold = 0.95;
            ev_config.battery_capacity_kWh = 50.0;
            ev_config.initial_soc = dist_ev_soc(rng_ev_soc);

            device_threads.emplace_back(device_thread_func, ev_config, std::ref(shared_freq_data));
            device_id_counter++;
        }
    }

    for (int i = 0; i < NUM_ESS_UNITS; ++i) {
        DeviceConfig ess_config;
        ess_config.id = device_id_counter;
        std::ostringstream name_stream;
        name_stream << "ESS单元_" << ess_config.id;
        ess_config.log_name = name_stream.str();

        ess_config.type = DeviceType::ESS_UNIT;
        ess_config.base_power_kW = 0.0;
        ess_config.gain_kW_per_Hz = 1000.0 / 0.03;
        ess_config.deadband_Hz = 0.03;
        ess_config.max_output_kW = 1000.0;
        ess_config.min_output_kW = -1000.0;
        ess_config.soc_min_threshold = 0.05;
        ess_config.soc_max_threshold = 0.95;
        ess_config.battery_capacity_kWh = 2000.0;
        ess_config.initial_soc = 0.7; // 固定初始SOC (同HECS)

        device_threads.emplace_back(device_thread_func, ess_config, std::ref(shared_freq_data));
        device_id_counter++;
    }

    std::cout << "信息: 已启动 " << device_threads.size() << " 个设备线程。" << std::endl;
    std::cout << "信息: 仿真将运行 " << SIMULATION_DURATION_SECONDS << " 秒 (模拟时间)..." << std::endl;

    if (oracle_thread.joinable()) {
        oracle_thread.join();
    }
    // std::cout << "调试: 频率预言机线程已汇合。" << std::endl;

    for (auto& th : device_threads) {
        if (th.joinable()) {
            th.join();
        }
    }
    // std::cout << "调试: 所有设备线程已汇合。" << std::endl;

    if (g_data_logger.is_open()) {
        g_data_logger.close();
    }

    auto real_time_sim_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> real_time_elapsed_seconds = real_time_sim_end - real_time_sim_start;

    std::cout << "\n--- 传统多线程仿真已结束 --- " << std::endl;
    std::cout << "模拟的总时长: " << SIMULATION_DURATION_SECONDS << " 秒。" << std::endl;
    std::cout << "真实物理执行耗时: " << std::fixed << std::setprecision(3) << real_time_elapsed_seconds.count() << " 秒。" << std::endl;

    long peak_mem_kb = get_peak_memory_usage_kb_traditional();
    if (peak_mem_kb != -1) {
        std::cout << "峰值内存使用 (近似值): " << peak_mem_kb << " KB (约 "
                  << std::fixed << std::setprecision(2) << peak_mem_kb / 1024.0 << " MB)。" << std::endl;
    } else {
        std::cout << "警告: 未能获取峰值内存使用数据。" << std::endl;
    }
    std::cout << "仿真结果已保存到文件: traditional_threaded_vpp_results.csv" << std::endl;

    return 0;
}