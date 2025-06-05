// logging_utils.cpp
// 实现了 logging_utils.h 中声明的日志初始化和关闭函数。
#include "logging_utils.h"
#include "spdlog/sinks/basic_file_sink.h" // 用于创建线程安全的文件日志输出目标 (sink)
#include "spdlog/sinks/stdout_color_sinks.h" // 用于创建线程安全的彩色控制台日志输出目标 (sink)
#include <iostream> // 用于在日志初始化本身失败时，通过 std::cerr 输出错误信息

// 定义全局日志记录器实例 (在 .h 文件中声明为 extern，此处是其实际定义)
std::shared_ptr<spdlog::logger> g_console_logger;
std::shared_ptr<spdlog::logger> g_data_file_logger;

// initialize_loggers 函数实现
void initialize_loggers(const std::string& data_log_filename, bool truncate_data_log)
{
    try {
        // 1. 配置和创建控制台日志记录器 (Console Logger)
        //    spdlog::stdout_color_mt 创建一个线程安全 (mt) 的、支持彩色的标准输出日志记录器。
        //    "控制台" 是这个日志记录器的名称，会出现在日志输出中 (如果格式包含 %n)。
        g_console_logger = spdlog::stdout_color_mt("控制台");
        // 设置控制台日志记录器的最低日志级别。只有等于或高于此级别的日志消息才会被输出。
        // 可选级别: trace, debug, info, warn, error, critical, off。
        g_console_logger->set_level(spdlog::level::info); // 例如，只显示 info 及更高级别的消息
        // 设置控制台日志的输出格式。
        // [%H:%M:%S.%e] : 时:分:秒.毫秒 (时间戳)
        // [%n] : 日志记录器名称 ("控制台")
        // [%^%l%$] : 带颜色标记的日志级别 (如 [info], [warn])
        // %v : 实际的日志消息内容
        g_console_logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");

        // 2. 配置和创建数据文件日志记录器 (Data File Logger)
        //    spdlog::basic_logger_mt 创建一个线程安全的基础文件日志记录器。
        //    "数据文件" 是此记录器的名称。
        //    data_log_filename 是日志文件的路径和名称。
        //    truncate_data_log 参数决定是在文件末尾追加日志 (false) 还是覆盖旧文件 (true)。
        g_data_file_logger = spdlog::basic_logger_mt("数据文件", data_log_filename, truncate_data_log);
        g_data_file_logger->set_level(spdlog::level::info); // 数据文件也记录 info 及以上级别
        // 为数据文件设置一个极简的日志格式，通常只包含消息本身 (%v)。
        // 这使得数据文件更易于被其他程序 (如CSV解析器、脚本) 处理。
        g_data_file_logger->set_pattern("%v");
        // 注意：spdlog 的文件 sink 通常有内部缓冲。为确保数据完全写入，
        // 依赖于程序结束时调用 shutdown_loggers() 中的 flush()。
        // 也可以通过 g_data_file_logger->flush_on(spdlog::level::info); 让其在每条info消息后刷新，但会影响性能。

        // 将控制台日志记录器设置为spdlog的默认记录器。
        // 这样，如果代码中直接调用 spdlog::info("...") 等全局函数，
        // 它们将默认使用 g_console_logger 进行输出。
        spdlog::set_default_logger(g_console_logger);
        spdlog::info("日志记录器已成功初始化。数据将记录到控制台及文件 '{}'。", data_log_filename);

    } catch (const spdlog::spdlog_ex& ex) {
        // 如果在初始化spdlog时发生异常 (例如，文件权限问题导致无法创建日志文件)，
        // 则捕获异常并通过 std::cerr 输出错误信息，因为此时spdlog可能尚不可用。
        std::cerr << "日志系统初始化失败: " << ex.what() << std::endl;
        // 根据应用需求，此处可以决定是否中止程序，例如:
        // exit(EXIT_FAILURE);
    }
}

// shutdown_loggers 函数实现
void shutdown_loggers()
{
    // 在关闭spdlog之前，可以通过日志记录器输出一条最终消息。
    if (g_console_logger) {
        // 注意：如果 initialize_loggers 失败，g_console_logger 可能为空。
        g_console_logger->info("正在刷新所有日志记录并准备关闭日志系统...");
    }

    // 显式刷新数据文件日志记录器，确保所有缓冲的日志消息都被写入到磁盘文件。
    // 这一点对于文件日志尤为重要，以防止程序退出时丢失未写入的数据。
    if (g_data_file_logger) {
        g_data_file_logger->flush();
    }
    // 理论上控制台输出通常不需要显式刷新，但为了完整性和应对某些特殊情况，也可以刷新。
    if (g_console_logger) {
        g_console_logger->flush();
    }

    // 调用 spdlog::shutdown() 来关闭整个spdlog日志系统。
    // 这会释放spdlog分配的所有资源，并确保所有异步日志记录器（如果使用了）完成其队列中的消息处理。
    // 这是良好实践，应在程序正常退出前调用。
    spdlog::shutdown();
}