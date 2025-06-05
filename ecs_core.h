// ecs_core.h
// 定义了实体组件系统 (ECS) 的核心结构和类。
// ECS是一种设计模式，常用于游戏开发和模拟，以促进数据和逻辑的分离，提高灵活性和性能、

#ifndef ECS_CORE_H
#define ECS_CORE_H

#include "logging_utils.h"
#include <cstdint> // 用于 uint64_t 等固定宽度整数类型
#include <memory> // 用于 std::unique_ptr 等智能指针，实现组件的自动内存管理
#include <type_traits> // 用于 std::is_base_of 等类型特性判断，例如在编译期检查组件是否继承自IComponent
#include <typeinfo> // 用于 typeid 获取类型信息 (例如计算哈希值作为组件类型的唯一标识)
#include <unordered_map> // 用于 std::unordered_map，提供高效的基于哈希的组件存储和检索
// 定义实体ID (Entity ID) 类型，使用64位无符号整数。
// 这提供了足够大的ID空间，以容纳大量实体。
using Entity = uint64_t;

// 组件接口基类 (Interface Component)
// 所有具体的组件类都应公有继承自 IComponent。
// 这个基类主要用于类型擦除和统一管理不同类型的组件。
// 它提供了一个虚析构函数，以确保通过基类指针删除派生类对象时，能够正确调用派生类的析构函数，防止资源泄漏。
class IComponent {
public:
    virtual ~IComponent() = default; // 默认的虚析构函数
};

// 注册表类 (Registry)
// Registry 是ECS架构的核心，负责管理所有实体 (Entities) 及其关联的组件 (Components)。
// 它提供以下功能：
// - 创建实体 (Entity creation)
// - 为实体添加组件 (Adding components to entities)
// - 从实体获取组件 (Retrieving components from entities)
// - 迭代具有特定类型组件的实体 (Iterating over entities with specific components)
class Registry {
public:
    // 创建一个新的实体并返回其唯一ID。
    // 实现方式是简单地递增一个内部ID计数器。
    // 注意：在更复杂的系统中，可能需要更完善的ID回收和管理机制。
    Entity create() { return ++last_id_; }

    // 为指定的实体 `e` 添加一个类型为 `Comp` 的组件，并就地构造它。
    // `Args&&... args` 是传递给组件 `Comp` 构造函数的参数列表 (使用完美转发)。
    // 静态断言 `static_assert` 确保 `Comp` 类型必须是 `IComponent` 的派生类。
    // 返回对新创建并添加到实体上的组件的引用。
    template <typename Comp, typename... Args>
    Comp& emplace(Entity e, Args&&... args)
    {
        // 编译期检查：确保组件类型 Comp 继承自 IComponent
        static_assert(std::is_base_of<IComponent, Comp>::value, "组件类型必须公有继承自 IComponent");

        // 使用 std::make_unique 创建组件的智能指针实例，实现自动内存管理
        auto ptr = std::make_unique<Comp>(std::forward<Args>(args)...);
        Comp* raw_ptr = ptr.get(); // 获取原始指针，用于返回对组件的引用

        // 存储组件：
        // components_ 是一个嵌套的 unordered_map。
        // 外层map的键是组件类型的哈希码 (通过 typeid(Comp).hash_code() 获取)。
        // 内层map的键是实体ID (e)。
        // 值是 std::unique_ptr<IComponent>，指向实际的组件对象。
        components_[typeid(Comp).hash_code()][e] = std::move(ptr);
        return *raw_ptr; // 返回对新创建组件的引用
    }

    // 获取实体 `e` 的 `Comp` 类型组件的指针。
    // 如果实体 `e` 没有该类型的组件，或者系统中不存在 `Comp` 类型的组件映射，则返回 `nullptr`。
    // 同样使用静态断言确保 `Comp` 是 `IComponent` 的派生类。
    template <typename Comp>
    Comp* get(Entity e)
    {

        static_assert(std::is_base_of<IComponent, Comp>::value, "组件类型必须公有继承自 IComponent");

        // Valgrind 指向这里或其内部实现
        auto comp_map_it = components_.find(typeid(Comp).hash_code()); // ecs_core.h:72
        if (comp_map_it == components_.end()) {
            if (g_console_logger)
                g_console_logger->trace("[Registry::get] Component type map not found.");
            else
                std::printf("[Registry::get] Component type map not found.\n");
            return nullptr;
        }

        auto entity_map_it = comp_map_it->second.find(e);
        if (entity_map_it == comp_map_it->second.end()) {
            if (g_console_logger)
                g_console_logger->trace("[Registry::get] Entity not found in component map.");
            else
                std::printf("[Registry::get] Entity not found in component map.\n");
            return nullptr;
        }

        Comp* component_ptr = static_cast<Comp*>(entity_map_it->second.get());
        if (g_console_logger)
            g_console_logger->trace("[Registry::get] Component found: %p", (void*)component_ptr);
        else
            std::printf("[Registry::get] Component found: %p\n", (void*)component_ptr);

        return component_ptr;
    }

    // 遍历所有具有 `Comp` 类型组件的实体。
    // 对每一个拥有 `Comp` 类型组件的实体，调用提供的函数对象 (回调函数) `fn`。
    // `fn` 应接受两个参数：对组件 `Comp` 的引用和实体ID `Entity`。
    // 例如: registry.for_each<PositionComponent>([](PositionComponent& pos, Entity id) { /* ... */ });
    template <typename Comp, typename Fn>
    void for_each(Fn&& fn)
    {
        static_assert(std::is_base_of<IComponent, Comp>::value, "组件类型必须公有继承自 IComponent");

        // 查找存储该类型组件的映射
        auto comp_map_it = components_.find(typeid(Comp).hash_code());
        if (comp_map_it != components_.end()) { // 确保该组件类型的映射存在
            // 遍历该组件类型映射中的所有实体-组件对
            for (auto const& [entity, component_ptr] : comp_map_it->second) {
                // 调用回调函数 fn，传入对组件的引用 (通过 static_cast 转换) 和实体ID
                fn(static_cast<Comp&>(*component_ptr), entity);
            }
        }
    }

private:
    Entity last_id_ { 0 }; // 用于生成下一个可用实体ID的计数器，从0开始递增。

    // 组件存储结构:
    // std::unordered_map<size_t, std::unordered_map<Entity, std::unique_ptr<IComponent>>> components_;
    // - 外层 `std::unordered_map` 的键 (`size_t`) 是组件类型的哈希码 (来自 `typeid(CompT).hash_code()`)。
    //   这允许按组件类型快速定位到相关的组件集合。
    // - 内层 `std::unordered_map` 的键 (`Entity`) 是实体ID。
    //   这允许在特定组件类型的集合中，按实体ID快速定位到该实体的组件实例。
    // - 值 (`std::unique_ptr<IComponent>`) 是指向实际组件对象的智能指针。
    //   使用 `std::unique_ptr` 确保了组件对象的自动内存管理 (当组件被移除或注册表销毁时)。
    //   存储的是基类指针 `IComponent*`，实现了类型擦除，允许在同一个结构中管理不同类型的组件。
    std::unordered_map<size_t, std::unordered_map<Entity, std::unique_ptr<IComponent>>> components_;
};

#endif // ECS_CORE_H