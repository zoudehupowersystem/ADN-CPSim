// cps_coro_lib.h
// 轻量级的、仅包含头文件的 C++20 协程库
// 用于多任务处理和事件驱动编程。它专为需要显式控制任务执行和时间进展的模拟或应用程序而设计
// 如何使用:
// -------------
// 1. 定义返回 `cps_coro::Task` 的函数以创建协程。
// 2. 在这些协程内部，使用 `co_await` 配合 `cps_coro::delay` 进行基于时间的挂起，
//    或配合 `cps_coro::wait_for_event` 等待特定事件。
// 3. 创建一个 `cps_coro::Scheduler` 实例。
// 4. 实例化协程 `Task`。当在调度器上下文中使用可等待对象时，调度器通过
//    线程局部指针隐式关联。
// 5. 调用调度器的方法，如 `run_one_step()` 或 `run_until(time_point)` 来执行已调度的任务。
// 6. 使用 `scheduler.trigger_event(event_id, data)` 或
//    `scheduler.trigger_event(event_id)` 来触发事件。
//
// 为构建离散事件模拟或其他合作式多任务系统提供一个简单而灵活的框架

#ifndef CPS_CORO_LIB_H
#define CPS_CORO_LIB_H

#include <chrono> // 用于时间和持续时间相关的 std::chrono 功能
#include <coroutine> // 用于C++20协程支持 (std::coroutine_handle, std::suspend_always 等)
#include <cstdint> // 用于固定宽度的整数类型，如 uint64_t (用于 EventId)
#include <exception> // 用于异常处理，如 std::terminate
#include <functional> // 用于 std::function (用于事件处理器)
#include <iostream> // <--- 已添加: 用于潜在的调试输出 (可选, 如果不调试实时调度器则可移除)
#include <map> // 用于 std::multimap (用于存储定时任务和事件处理器)
#include <memory> // 用于智能指针 (虽然在此文件中未直接大量使用，但常与协程库一起用于资源管理)
#include <queue> // 用于 std::queue (用于存储就绪任务)
#include <thread> // <--- 已添加: 用于 RealTimeScheduler 中的 std::this_thread::sleep_for
#include <utility> // 用于 std::pair, std::move 等通用工具
#include <variant> // 用于 std::variant (虽然在此文件中未直接使用，但可用于更复杂的事件数据传递)
#include <vector> // 用于 std::vector (例如在 trigger_event 中临时存储处理器)

namespace cps_coro { // 协程相关的命名空间

// 前向声明 Scheduler 类，以便 AwaiterBase 可以引用它
class Scheduler;

// 协程等待体 (Awaitable) 的基类
// AwaiterBase 是所有具体等待体 (Awaiter) 的抽象基类。
// 它提供了一个静态的线程局部变量 `active_scheduler_`，用于让等待体能够隐式访问当前线程的活动调度器实例。
// 这避免了在每次 `co_await` 时显式传递调度器。
// 友元类 Scheduler 可以访问其保护成员，特别是设置 `active_scheduler_`。
class AwaiterBase {
protected:
    // 指向当前线程活动的调度器实例的指针。
    // 当 Scheduler 对象被创建时，它会将自身注册为当前线程的活动调度器。
    // 当 Scheduler 对象被销毁时，它会清除此指针。
    inline static thread_local Scheduler* active_scheduler_ = nullptr;

    // 允许 Scheduler 类访问 AwaiterBase 的保护成员和私有成员。
    friend class Scheduler;

    // 默认构造函数
    AwaiterBase() = default;
};

// 事件ID类型定义，使用64位无符号整数，以提供广阔的事件ID空间。
using EventId = uint64_t;

// Task 类代表一个协程任务
// Task 是一个封装了协程句柄 (std::coroutine_handle) 的类，代表一个可执行的异步操作单元。
// 它通常用于表示没有返回值的协程 (void-returning coroutine)。
// 它负责协程的生命周期管理，包括创建、销毁、移动语义和恢复执行。
// promise_type 是协程的“契约”类型，定义了协程如何与编译器和Task对象交互，
// 例如如何创建Task对象、如何处理协程的返回值和未捕获的异常。
class Task {
public:
    // 协程的 promise_type，这是C++协程机制的一部分，用于编译器生成代码。
    struct promise_type {
        // 当协程启动时，此函数被调用以获取与此 promise 相关联的 Task 对象。
        Task get_return_object()
        {
            return Task { std::coroutine_handle<promise_type>::from_promise(*this) };
        }
        // 定义协程的初始挂起策略：std::suspend_never 表示协程在启动后不立即挂起，而是直接开始执行。
        std::suspend_never initial_suspend() { return {}; }
        // 定义协程的最终挂起策略：std::suspend_always 表示协程在执行完毕后总是挂起。
        // 这允许 Task 对象的析构函数或其他外部机制来销毁协程句柄，从而控制协程状态的清理。
        std::suspend_always final_suspend() noexcept { return {}; }
        // 当协程通过 co_return; (无值返回) 结束时调用此函数。
        void return_void() { }
        // 当协程内部有未捕获的异常时调用此函数。默认行为是终止程序。
        void unhandled_exception() { std::terminate(); }
    };

    // 默认构造函数，创建一个无效的 Task 对象。
    Task() = default;
    // 通过协程句柄构造 Task 对象。
    // explicit 关键字防止隐式转换。
    explicit Task(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    // 析构函数：如果协程句柄有效 (非空) 且协程尚未执行完毕 (done()返回false)，则销毁协程句柄。
    // 这是确保协程资源被释放的关键部分，特别是当 final_suspend 返回 std::suspend_always 时。
    ~Task()
    {
        if (handle_ && !handle_.done()) {
            handle_.destroy();
        }
    }

    // 禁止拷贝构造函数，Task 对象不应被拷贝，因为它们唯一地拥有协程句柄。
    Task(const Task&) = delete;
    // 禁止拷贝赋值运算符。
    Task& operator=(const Task&) = delete;

    // 移动构造函数，允许 Task 对象的高效转移。
    // 原对象 other 的句柄被置空，以确保句柄所有权的正确转移。
    Task(Task&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr; // 原对象的句柄置空，防止其析构函数重复销毁句柄
    }

    // 移动赋值运算符。
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) { // 防止自赋值
            // 如果当前 Task 对象持有一个有效且未完成的协程，先销毁它
            if (handle_ && !handle_.done()) {
                handle_.destroy();
            }
            handle_ = other.handle_; // 从 other 对象获取协程句柄的所有权
            other.handle_ = nullptr; // 原对象的句柄置空
        }
        return *this;
    }

    // 分离协程句柄。调用此方法后，Task 对象不再负责协程的生命周期管理。
    // 调用者必须确保分离的协程句柄最终会被销毁，否则可能导致资源泄漏。
    // 适用于“即发即忘”(fire-and-forget) 类型的任务，或由其他机制管理生命周期的任务。
    void detach()
    {
        handle_ = nullptr;
    }

    // 检查协程是否已完成执行。
    // 如果句柄为空或协程已执行完毕 (handle_.done() 为 true)，则返回 true。
    bool is_done() const
    {
        return !handle_ || handle_.done();
    }

    // 恢复协程的执行。
    // 仅当协程句柄有效且协程当前处于挂起状态 (未完成) 时才执行恢复操作。
    void resume()
    {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

private:
    // 存储协程的句柄。
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

// 调度器类，负责管理和执行协程任务
// Scheduler 维护一个模拟的“当前时间”，以及三个核心数据结构：
// 1. 就绪任务队列 (ready_tasks_)：存储可以立即执行的协程。
// 2. 定时任务映射 (timed_tasks_)：存储需要在未来特定时间点执行的协程。
// 3. 事件处理器映射 (event_handlers_)：存储等待特定事件发生的协程的回调。
// 它提供了调度协程、处理延时、触发事件和运行协程的核心机制，是事件驱动仿真的心脏。
class Scheduler {
public:
    // 时间点类型定义，基于稳定时钟 (std::chrono::steady_clock)，精度为毫秒。
    // 稳定时钟保证时间点单调递增，不受系统时间调整影响。
    using time_point = std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds>;
    // 时间间隔类型定义，精度为毫秒。
    using duration = std::chrono::milliseconds;
    // 事件处理器函数类型，接受一个 const void* 参数 (用于传递任意类型的事件数据)。
    using EventHandler = std::function<void(const void*)>;

    // 构造函数：初始化当前模拟时间为0时刻，并将此调度器实例设置为当前线程的活动调度器。
    Scheduler()
        : current_time_(time_point { duration { 0 } }) // 仿真时间从0开始
    {
        AwaiterBase::active_scheduler_ = this; // 设置当前线程的活动调度器
    }

    // 析构函数：如果此调度器实例是当前线程的活动调度器，则清除该指针。
    // <--- 稍作修改: 将析构函数设为虚函数，以便安全继承 (例如 RealTimeScheduler)
    virtual ~Scheduler()
    {
        if (AwaiterBase::active_scheduler_ == this) {
            AwaiterBase::active_scheduler_ = nullptr;
        }
    }

    // 获取调度器的当前模拟时间。
    time_point now() const { return current_time_; }
    // 设置调度器的当前模拟时间。
    // 注意：直接设置时间可能跳过某些定时事件，应谨慎使用。
    void set_time(time_point new_time) { current_time_ = new_time; }
    // 将调度器的当前模拟时间向前推进指定的时长 `delta`。
    void advance_time(duration delta) { current_time_ += delta; }

    // 调度一个协程句柄，将其加入就绪任务队列，等待立即执行。
    // 就绪任务队列通常是先进先出 (FIFO) 的。
    void schedule(std::coroutine_handle<> handle)
    {
        ready_tasks_.push(handle);
    }

    // 调度一个协程句柄，在指定的延迟 `delay` 之后执行。
    // 任务会被添加到 `timed_tasks_` 多重映射中，按其计划唤醒时间排序。
    void schedule_after(duration delay, std::coroutine_handle<> handle)
    {
        // timed_tasks_ 是一个 std::multimap，键是唤醒时间点，值是协程句柄。
        // emplace 直接在容器中构造元素，避免不必要的拷贝或移动。
        timed_tasks_.emplace(current_time_ + delay, handle);
    }

    // 注册一个事件处理器。
    // 当具有特定 `event_id` 的事件被触发时，相应的 `handler` (一个 std::function) 将被调用。
    // 使用 std::multimap 允许多个处理器监听同一个事件ID。
    // 事件处理器通常由 `EventAwaiter` 在协程 `co_await` 事件时注册。
    void register_event_handler(EventId event_id, EventHandler handler)
    {
        event_handlers_.emplace(event_id, std::move(handler));
    }

    // 触发一个带有数据的事件。
    // `EventData` 是事件数据的类型。
    // 所有注册到此 `event_id` 的事件处理器都将被调用，并传入事件数据 `data` (通过 const void* 转换)。
    // 注意：在此实现中，处理器在被调用后会从 `event_handlers_` 中移除 (实现一次性触发逻辑)。
    // 这是因为 `EventAwaiter` 在被唤醒后通常任务已完成或会重新等待，不需要持久的处理器。
    template <typename EventData>
    void trigger_event(EventId event_id, const EventData& data)
    {
        auto range = event_handlers_.equal_range(event_id); // 获取所有匹配此 event_id 的处理器迭代器范围
        std::vector<EventHandler> handlers_to_call; // 临时存储待调用的处理器，防止在遍历时修改容器导致迭代器失效
        for (auto it = range.first; it != range.second; ++it) {
            handlers_to_call.push_back(it->second);
        }

        // 移除已找到的处理器 (实现一次性事件处理机制)
        // 如果希望事件处理器是持久性的，应修改此逻辑，例如不在此处移除。
        // 但根据 EventAwaiter 的典型行为 (它在被唤醒后通常会结束或重新等待新事件)，一次性处理器是合适的。
        if (range.first != range.second) {
            event_handlers_.erase(range.first, range.second);
        }

        // 调用处理器
        for (const auto& handler_func : handlers_to_call) {
            handler_func(static_cast<const void*>(&data)); // 将数据转换为 const void* 类型传递给通用的 EventHandler
        }
    }

    // 触发一个不带数据的事件 (void 事件)。
    // 所有注册到此 `event_id` 的处理器都会被调用，传入 `nullptr` 作为数据指针。
    // 同样，处理器在被调用后会从 `event_handlers_` 中移除。
    void trigger_event(EventId event_id)
    {
        auto range = event_handlers_.equal_range(event_id);
        std::vector<EventHandler> handlers_to_call;
        for (auto it = range.first; it != range.second; ++it) {
            handlers_to_call.push_back(it->second);
        }
        if (range.first != range.second) {
            event_handlers_.erase(range.first, range.second);
        }
        for (const auto& handler_func : handlers_to_call) {
            handler_func(nullptr); // 不带数据，传递 nullptr
        }
    }

    // 执行一步调度循环。这是调度器的核心“脉搏”。
    // 1. 优先检查并执行就绪队列中的任务。
    // 2. 如果就绪队列为空，则检查是否有到期的定时任务。
    // 3. 如果有到期的定时任务，将其移至就绪队列，并可能更新当前模拟时间到该任务的触发时间。
    // 返回 `true` 如果成功执行了任何操作 (例如恢复了一个任务或处理了定时任务)，否则返回 `false` (表示当前无事可做)。
    bool run_one_step()
    {
        // 阶段1: 处理就绪任务
        if (!ready_tasks_.empty()) {
            auto h = ready_tasks_.front(); // 获取队首的协程句柄
            ready_tasks_.pop(); // 从队列中移除该句柄
            if (!h.done()) { // 如果任务尚未完成
                h.resume(); // 恢复其执行
            }
            return true; // 成功执行了一个就绪任务
        }

        // 阶段2: 处理定时任务 (仅当没有就绪任务时)
        if (!timed_tasks_.empty()) {
            // (仅当该时间确实晚于当前时间 current_time_ 时才推进时间)
            // 将当前时间推进到最早的定时任务的计划执行时间
            // 这一步确保了时间只向前推移，并且是推移到下一个有事件发生的时刻
            if (timed_tasks_.begin()->first > current_time_) {
                set_time(timed_tasks_.begin()->first);
            }

            // 将所有已到期或早于当前模拟时间的定时任务移到就绪队列
            while (!timed_tasks_.empty() && timed_tasks_.begin()->first <= current_time_) {
                auto h = timed_tasks_.begin()->second; // 获取任务句柄
                timed_tasks_.erase(timed_tasks_.begin()); // 从定时任务映射中移除
                ready_tasks_.push(h); // 加入就绪队列
            }

            // 尝试在当前步骤中立即运行一个新就绪的任务 (如果刚才有任务从定时队列移入)
            if (!ready_tasks_.empty()) {
                auto h = ready_tasks_.front();
                ready_tasks_.pop();
                if (!h.done()) {
                    h.resume();
                }
            }
            return true; // 处理了定时任务 (即使只是将其移至就绪队列或也运行了一个新就绪的任务)
        }
        return false; // 当前仿真时刻没有就绪任务，也没有到期的定时任务
    }

    // 运行调度循环直到指定的结束时间 `end_time`，
    // 或者直到所有任务 (包括就绪任务和定时任务) 都已处理完毕。
    void run_until(time_point end_time)
    {
        // 当 当前模拟时间 < 目标结束时间 并且 (队列中仍有就绪任务 或 映射中仍有定时任务) 时，持续运行调度循环
        while (current_time_ < end_time && (!ready_tasks_.empty() || !timed_tasks_.empty())) {
            // 优先处理所有当前时刻的就绪任务
            while (!ready_tasks_.empty()) {
                auto h = ready_tasks_.front();
                ready_tasks_.pop();
                if (!h.done()) {
                    h.resume();
                }
            }

            // 如果就绪队列已空，但仍有定时任务等待执行
            if (ready_tasks_.empty() && !timed_tasks_.empty()) {
                time_point next_event_time = timed_tasks_.begin()->first; // 获取下一个最近的定时任务的触发时间

                // 如果下一个事件的计划时间晚于或等于指定的仿真结束时间
                if (next_event_time >= end_time) {
                    set_time(end_time); // 将当前模拟时间精确设置为结束时间
                    break; // 达到或超过仿真时长，退出循环
                }
                // 否则，将当前模拟时间推进到下一个事件的发生时间
                set_time(next_event_time);

                // 将所有在新的当前时间点或之前到期的定时任务移到就绪队列
                while (!timed_tasks_.empty() && timed_tasks_.begin()->first <= current_time_) {
                    auto h = timed_tasks_.begin()->second;
                    timed_tasks_.erase(timed_tasks_.begin());
                    ready_tasks_.push(h);
                }
                // 此时，ready_tasks_ 可能已非空，下一轮外层while循环会处理它们
            }
        }
        // 循环结束后，如果当前模拟时间仍早于指定的结束时间 (例如所有任务都已完成，仿真提前结束)
        // 将当前模拟时间设置为正式的结束时间，确保时间记录的准确性。
        if (current_time_ < end_time) {
            set_time(end_time);
        }
    }

    // 检查调度器是否为空 (即没有就绪任务、没有定时任务、也没有注册的事件处理器)。
    // 注意: 原始的 is_empty 包含了对 event_handlers_ 的检查。对于判断“所有任务是否完成”，
    // 通常只关心 ready_tasks_ 和 timed_tasks_。
    // 此处保留原始行为。如果事件处理器可以持久存在 (非一次性)，这个检查是合理的。
    // 如果它们总是一次性的 (触发后即清除)，那么当所有协程等待的事件都被触发后，event_handlers_ 最终也会变空。
    bool is_empty() const
    {
        return ready_tasks_.empty() && timed_tasks_.empty() && event_handlers_.empty();
    }

    // 检查是否有任何待处理的任务 (无论是就绪任务还是定时任务)。
    // 这个函数常用于判断仿真是否因为没有活动任务而可以结束。
    bool has_pending_tasks() const
    {
        return !ready_tasks_.empty() || !timed_tasks_.empty();
    }

private:
    // private: // 恢复为 private，因为 RealTimeScheduler 可以通过公共 API 实现其功能。

    time_point current_time_; // 调度器的当前模拟时间
    std::queue<std::coroutine_handle<>> ready_tasks_; // 就绪任务队列 (FIFO)，存储等待立即执行的协程句柄
    std::multimap<time_point, std::coroutine_handle<>> timed_tasks_; // 定时任务，按计划执行时间排序的多重映射
    std::multimap<EventId, EventHandler> event_handlers_; // 事件处理器，按事件ID组织的多重映射
};

// Delay 等待体 (Awaitable)，用于使协程暂停指定的时长
// 当在协程中使用 `co_await Delay(duration)` 时，当前协程会挂起。
// 调度器会在指定的 `duration` 时长过去后，自动恢复该协程的执行。
class Delay : public AwaiterBase {
public:
    // 构造函数，接受一个 `Scheduler::duration` 类型的延迟时间参数。
    explicit Delay(Scheduler::duration delay_time)
        : delay_time_(delay_time)
    {
    }

    // `await_ready` 方法：检查是否需要实际挂起协程。
    // 如果延迟时间小于或等于0，表示无需等待，协程可以继续立即执行，此时返回 `true`。
    // 否则返回 `false`，表示需要挂起。
    bool await_ready() const noexcept
    {
        return delay_time_.count() <= 0; // 如果延迟时间非正，则无需挂起
    }

    // `await_suspend` 方法：在协程挂起时调用。
    // `handle` 是当前被挂起的协程的句柄。
    // 此方法负责将协程的恢复操作安排给调度器。
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        Scheduler* scheduler = AwaiterBase::active_scheduler_; // 获取当前线程的活动调度器
        if (scheduler) {
            // 通知调度器在指定的 delay_time_ 之后恢复此协程 handle
            scheduler->schedule_after(delay_time_, handle);
        } else {
            // 如果没有活动的调度器 (例如在调度器上下文之外 co_await Delay)，
            // 则立即恢复协程 (相当于无延迟，或表示错误的使用方式)。
            handle.resume();
        }
    }
    // `await_resume` 方法：在协程恢复执行后调用。
    // 对于 `Delay` 等待体，恢复时不需要执行任何特殊操作，也不返回任何值 (void)。
    void await_resume() const noexcept { }

private:
    Scheduler::duration delay_time_; // 需要延迟的时长
};

// EventAwaiter 等待体 (Awaitable) (模板版本，用于等待带有数据的事件)
// 当在协程中使用 `co_await EventAwaiter<DataType>(eventId)` 时，当前协程会挂起，
// 直到系统中触发了具有指定 `eventId` 的事件。
// 当协程恢复时，`await_resume` 方法会返回事件附带的数据 (类型为 `EventData`)。
template <typename EventData = void> // 默认模板参数为 void, 但此主模板处理非 void 的情况
class EventAwaiter : public AwaiterBase {
public:
    // 构造函数，接受一个事件ID `event_id`。
    explicit EventAwaiter(EventId event_id)
        : event_id_(event_id)
    {
    }

    // `await_ready` 方法：始终返回 `false`，表示等待事件总是需要挂起协程。
    bool await_ready() const noexcept { return false; }

    // `await_suspend` 方法：在协程挂起时调用。
    // `handle` 是当前被挂起的协程的句柄。
    // 此方法向调度器注册一个事件处理器，当匹配的事件触发时，该处理器会恢复协程。
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        Scheduler* scheduler = AwaiterBase::active_scheduler_; // 获取当前线程的活动调度器
        if (scheduler) {
            // 注册一个Lambda表达式作为事件处理器
            scheduler->register_event_handler(event_id_,
                [this, h = handle](const void* data) mutable { // 捕获 this 和协程句柄 h
                    if (data) { // 如果事件确实带有数据 (data 指针非空)
                        // 将 const void* 类型的原始数据指针转换为具体的事件数据类型 EventData* 并解引用，
                        // 然后存储到成员变量 event_data_ 中。
                        event_data_ = *static_cast<const EventData*>(data);
                    }
                    h.resume(); // 恢复等待此事件的协程 h
                });
        } else {
            // 没有活动的调度器，立即恢复协程 (可能表示错误的使用场景)
            handle.resume();
        }
    }

    // `await_resume` 方法：在协程因事件触发而恢复执行后调用。
    // 返回存储的事件数据 `event_data_`。
    EventData await_resume() const noexcept { return event_data_; }

private:
    EventId event_id_; // 等待的事件ID
    EventData event_data_ {}; // 用于存储接收到的事件数据 (如果事件有数据)。使用 {} 进行值初始化。
};

// EventAwaiter 等待体 (Awaitable) (void 特化版本，用于等待不带数据的事件)
// 当在协程中使用 `co_await EventAwaiter<void>(eventId)` 或通过辅助函数 `co_await wait_for_event(eventId)` 时，
// 当前协程会挂起，直到具有指定 `eventId` 的事件被触发。
// 恢复时，`await_resume` 不返回任何值。
template <>
class EventAwaiter<void> : public AwaiterBase {
public:
    // 构造函数，接受一个事件ID `event_id`。
    explicit EventAwaiter(EventId event_id)
        : event_id_(event_id)
    {
    }
    // `await_ready` 方法：始终返回 `false`，表示等待事件总是需要挂起协程。
    bool await_ready() const noexcept { return false; }

    // `await_suspend` 方法：挂起协程并注册事件处理器。
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        Scheduler* scheduler = AwaiterBase::active_scheduler_;
        if (scheduler) {
            scheduler->register_event_handler(event_id_,
                // Lambda表达式作为事件处理器
                [h = handle](const void* /*data*/) mutable { // data 参数在此被忽略，因为是 void 事件
                    h.resume(); // 恢复等待此事件的协程 h
                });
        } else {
            handle.resume(); // 没有调度器，立即恢复
        }
    }
    // `await_resume` 方法：协程恢复后不返回任何值。
    void await_resume() const noexcept { }

private:
    EventId event_id_; // 等待的事件ID
};

// 便捷函数 (Helper Function)，用于创建 Delay 等待体实例。
// 使得 `co_await cps_coro::delay(duration)` 的写法成为可能。
inline Delay delay(Scheduler::duration duration)
{
    return Delay(duration);
}

// 便捷函数 (Helper Function)，用于创建 EventAwaiter 等待体实例。
// 允许通过 `co_await cps_coro::wait_for_event<DataType>(eventId)` 或
// `co_await cps_coro::wait_for_event(eventId)` (用于void事件) 的写法。
// 如果不显式指定模板参数 `EventData`，则默认为 `void`，对应 `EventAwaiter<void>` 特化版本。
template <typename EventData = void>
inline EventAwaiter<EventData> wait_for_event(EventId event_id)
{
    return EventAwaiter<EventData>(event_id);
}

// --- 实时仿真接口 ---
// RealTimeScheduler 类继承自 Scheduler，提供了将仿真时间与物理时钟时间对齐的功能。
class RealTimeScheduler : public Scheduler {
public:
    RealTimeScheduler()
        : Scheduler() // 调用基类 Scheduler 的构造函数
    {
        // AwaiterBase::active_scheduler_ 已在 Scheduler 基类构造函数中设置
    }

    // 运行调度器，尝试将仿真时间与物理时钟时间（挂钟时间）对齐，直到达到指定的仿真结束时间。
    void run_real_time_until(time_point end_simulation_time)
    {
        // 此运行方法实际启动时的物理时钟时间。
        auto wall_clock_physical_start = std::chrono::steady_clock::now();

        // 此运行方法启动时的仿真时间 (如果调度器是新创建的，通常为0)。
        time_point initial_sim_time_at_run_start = now();

        // 当 当前仿真时间 < 目标仿真结束时间 时，持续循环
        while (now() < end_simulation_time) {
            // 如果没有待处理的任务 (无论是就绪任务还是定时任务)，则对于任务而言，仿真实际上在此处结束。
            // 稍后我们将通过休眠来使物理时钟与 end_simulation_time 对齐。
            if (!has_pending_tasks()) { // 使用 has_pending_tasks 仅检查就绪/定时任务
                break; // 没有任务了，跳出循环
            }

            // // 在可能推进时间或处理任务之前存储仿真时间 (用于调试或更细粒度的休眠，当前未使用)
            // time_point sim_time_before_step = now();

            // 执行一步调度。run_one_step 将会:
            // 1. 执行一个就绪任务 (此时仿真时间 current_time_ 本身不会推进)。
            // 2. 或者，如果没有就绪任务，则将 current_time_ 推进到下一个定时任务的时间，并将其移至就绪队列, 然后可能执行它。
            // 如果执行了工作 (任务被恢复或定时任务被处理)，则返回 true。
            /* bool work_done_this_step = */ run_one_step();

            // 调用 run_one_step 后，如果一个定时任务到期被处理，now() (即 current_time_) 可能已经前进。
            // 或者，如果处理了一个就绪任务，now() 保持不变 (因为就绪任务不直接推进仿真时间)。
            // 我们需要确保物理时钟“追赶”上当前的仿真时间。

            // 根据调度器的时钟，到目前为止应该已经流逝的总仿真时间。
            duration total_sim_time_elapsed_so_far = now() - initial_sim_time_at_run_start;

            // 目标物理时钟时间：物理启动时间 + 已流逝的总仿真时间。
            // 这是我们期望物理时钟到达的时间点，以匹配当前的仿真进度。
            auto target_wall_time = wall_clock_physical_start + total_sim_time_elapsed_so_far;

            auto current_wall_time = std::chrono::steady_clock::now(); // 获取当前的物理时钟时间

            // 如果目标物理时钟时间晚于当前物理时钟时间，意味着仿真“跑得太快”，需要休眠等待物理时钟跟上。
            if (target_wall_time > current_wall_time) {
                std::this_thread::sleep_for(target_wall_time - current_wall_time);
            }
            // 如果 current_wall_time >= target_wall_time，表示仿真运行缓慢或准时，不需要休眠。物理时钟已经追上或超过了仿真进度。
        }

        // 循环结束后，任务可能已全部完成，或者仿真时间 now() 已达到 end_simulation_time。
        // 如果所有任务在仿真时间 now() 到达 end_simulation_time 之前就已完成，我们需要
        // 确保物理时钟“等待”直到相当于 end_simulation_time 的时刻。
        if (now() < end_simulation_time) {
            // 计算从运行开始到目标仿真结束时间总共需要流逝的仿真时长
            duration final_sim_duration_to_achieve = end_simulation_time - initial_sim_time_at_run_start;
            // 计算最终的目标物理时钟时间
            auto final_target_wall_time = wall_clock_physical_start + final_sim_duration_to_achieve;
            auto current_wall_time_at_end_loop = std::chrono::steady_clock::now(); // 获取循环结束时的当前物理时间

            // 如果最终目标物理时钟时间晚于当前物理时钟时间，则休眠差额
            if (final_target_wall_time > current_wall_time_at_end_loop) {
                std::this_thread::sleep_for(final_target_wall_time - current_wall_time_at_end_loop);
            }
            // 同时，将调度器的当前仿真时间设置为正式的结束时间。
            set_time(end_simulation_time);
        }
        // 如果 now() >= end_simulation_time，仿真已自然进行到或超过其结束点。
        // 此处无需对时间进行特殊处理。run_one_step 可能导致仿真时间略微超出 end_simulation_time，
        // 例如当最后一个定时任务的触发时间略晚于 end_simulation_time 时。
        // 对于此实现，允许 now() 略微超过 end_simulation_time 是可接受的。
    }
};

} // namespace cps_coro

#endif // CPS_CORO_LIB_H