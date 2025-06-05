// logging_utils.h
// 提供全局日志记录功能的初始化和关闭工具。
// 使用 spdlog 库作为底层的日志框架。
#ifndef LOGGING_UTILS_H
#define LOGGING_UTILS_H

#include "spdlog/spdlog.h" // spdlog 日志库主头文件
#include <memory> // 用于 std::shared_ptr (智能指针管理日志记录器对象)
#include <string> // 用于 std::string (例如文件名)

// 全局日志记录器实例 (声明)
// 这些共享指针将在 initialize_loggers 函数中被创建和初始化。
// 通过 extern 关键字，这些变量可以在项目的其他 .cpp 文件中被访问。
extern std::shared_ptr<spdlog::logger> g_console_logger; // 指向控制台日志记录器的共享指针
extern std::shared_ptr<spdlog::logger> g_data_file_logger; // 指向数据文件日志记录器的共享指针 (例如用于输出CSV格式的仿真数据)

// 初始化日志记录器函数
// 此函数应在程序启动早期被调用，以配置和创建全局日志记录器。
// data_log_filename: 指定数据日志文件的名称。默认为 "simulation_output.csv"。
// truncate_data_log: 一个布尔值，指示是否在每次程序启动时清空 (截断) 已存在的数据日志文件。
//                    如果为 true (默认)，则每次运行都会创建一个新的日志文件 (或覆盖同名旧文件)。
//                    如果为 false，则日志消息会追加到现有文件的末尾。
void initialize_loggers(const std::string& data_log_filename = "仿真输出数据.csv", bool truncate_data_log = true);

// 函数用于在程序结束前刷新和关闭日志记录器。
// 调用此函数非常重要，以确保所有缓冲的日志消息都被实际写入到文件或控制台，
// 并正确释放spdlog库所占用的资源。
void shutdown_loggers();

#endif // LOGGING_UTILS_H