# 主动配电网CPS统一行为建模与高效仿真平台 / Unified Behavior Modeling and Efficient Simulation Platform for Active Distribution Network CPS

## 1. 项目概述 / Project Overview

ADN-CPSim(Active Distribution Network Cyber-Physical Simulation) 是一个基于C++20开发的高性能仿真平台，旨在为主动配电网 (Active Distribution Network, ADN) 中日益复杂和多样化的信息物理系统 (Cyber-Physical System, CPS) 应用提供统一的行为建模与高效的事件驱动仿真环境。**本项目当前主要作为学术研究的简化测试版本，用于验证所提出的建模与仿真方法的可行性与高效性。**

ADN-CPSim (Active Distribution Network Cyber-Physical Simulation) is a high‑performance simulation platform built with C++20. It provides a unified behavior modeling and efficient event‑driven environment for the increasingly complex CPS applications in Active Distribution Networks. **The project currently serves as a simplified academic testbed for validating proposed modeling and simulation methods.**

随着分布式能源、储能、电动汽车以及各类智能感知与控制装置在配电网中的广泛应用，传统的电力系统仿真工具在统一描述这些跨物理层与信息层、行为异构的业务，并高效处理其复杂的交互逻辑方面面临挑战。本项目提出的实体-组件-拓扑-系统（ECTS）架构与基于C++20协程的事件驱动仿真引擎，致力于解决这些难题。

With the proliferation of distributed energy, storage, EVs, and intelligent sensing/control devices, traditional tools struggle to describe cross‑layer heterogeneous behaviors and handle their complex interactions efficiently. The proposed Entity‑Component‑Topology‑System (ECTS) architecture and C++20 coroutine‑based event‑driven engine address these challenges.

**目标用户：** 电力系统工程师、研究人员、从事主动配电网、微电网、虚拟电厂 (VPP)、CPS建模与仿真相关工作的开发者，**尤其适合对新型仿真技术进行学术探索与验证的场景。**

**Target users:** power‑system engineers, researchers, and developers working on ADN, microgrids, VPPs, or CPS modeling and simulation—**especially those exploring and validating new simulation techniques.**

## 2. 项目特点与创新 / Features & Innovations

* **统一的行为建模框架 (ECTS)**：
    * **分层解耦**：通过实体（Entity）、组件（Component）、系统（System，由协程任务实现）、拓扑四个部分，清晰地描述了物理设备（如IED、DTU、充电桩、储能单元）和信息系统应用（如VPP控制逻辑、馈线自动化主站）的行为。
    * **行为等效性**：组件模型遵循行为等效原则，关注设备或应用在特定业务场景下的外部行为特性和交互逻辑，而非精确复刻其内部复杂机理，显著降低了模型复杂度，提高了建模效率和通用性。
    * **模块化与可扩展性**：新的设备类型或CPS应用可以通过定义新的行为组件轻松集成到仿真平台中，具有良好的可扩展性。

* **Unified Behavior Modeling Framework (ECTS):**
    * **Layered decoupling:** Entities, Components, Systems (implemented as coroutine tasks), and Topology clearly describe behaviors of physical devices (IEDs, DTUs, chargers, storage units) and information‑system applications (VPP control logic, feeder automation masters).
    * **Behavioral equivalence:** Component models focus on external behaviors and interaction logic for specific scenarios rather than reproducing complex internal mechanisms, reducing modeling complexity and improving efficiency and generality.
    * **Modularity and extensibility:** New device types or CPS applications can be integrated easily by defining new behavior components.

* **基于C++20协程的高效事件驱动仿真引擎**：
    * **轻量级并发**：利用C++20协程实现用户态的轻量级并发，能够以极低的开销管理成千上万个并发执行的仿真对象（如每个充电桩的行为逻辑）。
    * **事件驱动核心**：仿真过程由离散事件驱动，仅当关键事件发生（如频率更新、故障注入）或特定条件满足（如VPP控制逻辑中的频率偏差阈值、时间阈值）时，才触发相关对象的行为计算与状态更新，大幅减少了不必要的计算，提高了仿真效率。
    * **异步逻辑的同步化表达**：通过 `co_await` 关键字，复杂的异步等待（如延时、等待外部事件）可以用同步的方式清晰表达，显著提升了代码的可读性和可维护性。
    * **多时间尺度支持**：能够自然地处理保护（毫秒级）、控制（秒/毫秒级）、调度（分钟/小时级）等不同时间尺度和响应频率的CPS应用。
    * **实时与非实时仿真模式**：平台内置了标准事件调度器 (`Scheduler`) 用于非实时仿真（尽可能快地执行），以及实时调度器 (`RealTimeScheduler`)，可尝试将仿真时间的推进与物理时钟同步，适用于需要与真实时间对齐的测试场景。

* **High‑performance event‑driven simulation engine based on C++20 coroutines:**
    * **Lightweight concurrency:** User‑space coroutines manage thousands of concurrent objects (e.g., charger behaviors) with minimal overhead.
    * **Event‑driven core:** Simulation advances only when key events occur (frequency updates, fault injections) or conditions are met (VPP thresholds), reducing unnecessary computation and improving efficiency.
    * **Synchronous expression of asynchronous logic:** The `co_await` keyword expresses complex waits (delays, external events) in a clear synchronous style, enhancing readability and maintainability.
    * **Multi‑time‑scale support:** Naturally handles CPS applications with protection (ms), control (s/ms), and scheduling (min/h) time scales.
    * **Real‑time and non‑real‑time modes:** Includes a standard `Scheduler` for fast non‑real‑time runs and a `RealTimeScheduler` that aligns simulation time with wall‑clock time for real‑time tests.

* **高性能日志记录**：
    * 集成 `spdlog` 库，提供高性能、异步的日志记录功能，支持将详细的仿真数据在程序结束时统一写入文件（如CSV格式），减少了仿真过程中的I/O瓶颈。

* **High‑performance logging:**
    * Integrates the `spdlog` library for asynchronous, high‑performance logging, allowing detailed simulation data to be written to files (e.g., CSV) at program end to reduce I/O bottlenecks.

## 3. 关键技术栈 / Tech Stack

* **编程语言**: C++20 (充分利用其协程、模板、并发等新特性)
* **核心设计模式**:
    * 实体-组件-系统 (ECS) 架构 (本项目中体现为HECS)
    * 事件驱动架构 (Event-Driven Architecture, EDA)
* **并发模型**: C++20 Coroutines
* **日志库**: [spdlog](https://github.com/gabime/spdlog)
* **构建系统**: CMake

* **Programming Language:** C++20 (leveraging coroutines, templates, and concurrency features)
* **Core Design Patterns:**
    * Entity‑Component‑System (ECS) architecture (implemented here as HECS)
    * Event‑Driven Architecture (EDA)
* **Concurrency Model:** C++20 coroutines
* **Logging Library:** [spdlog](https://github.com/gabime/spdlog)
* **Build System:** CMake

## 4. 项目结构 / Project Structure

项目采用扁平化的源码目录结构，主要源文件位于仓库根目录，每个系统或示例以独立的 `*.cpp`/`*.h` 文件呈现。构建配置通过根目录下的 `CMakeLists.txt` 管理，样例程序如 `avc_simulation.cpp`、`vpp_system.cpp` 展示不同的业务逻辑。

The project uses a flat source tree with core source files in the repository root, each system or example implemented in separate `*.cpp` and `*.h` files. The root `CMakeLists.txt` manages the build, and example programs such as `avc_simulation.cpp` and `vpp_system.cpp` showcase different application logic.

## 5. 已实现功能与仿真案例介绍 / Implemented Features & Simulation Examples

本项目当前已实现并演示了以下核心功能和应用场景（**作为简化测试版本，部分功能为原理性演示**）：

This project currently implements and demonstrates the following core features and scenarios (**as a simplified test version, some features are conceptual demonstrations**):

### 5.1 基础仿真引擎 (`cps_coro_lib.h`, `ecs_core.h`) / Basic Simulation Engine

* 基于协程的离散事件调度器，支持非实时 (`Scheduler`) 和实时 (`RealTimeScheduler`) 模式。
* 协程任务 (`Task`) 的封装与生命周期管理。
* 基于时间的挂起 (`cps_coro::delay`) 和基于事件的等待 (`cps_coro::wait_for_event`) 机制。
* 实体 (`Entity`) 与行为组件 (`IComponent`) 的注册、存储和管理 (`Registry`)。

* Coroutine‑based discrete event scheduler supporting non‑real‑time (`Scheduler`) and real‑time (`RealTimeScheduler`) modes.
* Encapsulation and lifecycle management of coroutine tasks (`Task`).
* Time‑based suspension (`cps_coro::delay`) and event‑based waiting (`cps_coro::wait_for_event`).
* Registration, storage, and management of entities (`Entity`) and behavior components (`IComponent`) via the `Registry`.

### 5.2 仿真案例一：自动电压控制 (AVC) 场景 / Case 1: Automatic Voltage Control

* **文件**: `avc_simulation.cpp`
* **目标**: 演示平台在模拟包含传感器、控制器和监测器等多智能体交互的复杂CPS应用场景的能力，并对比非实时与实时仿真模式。

* **File:** `avc_simulation.cpp`
* **Purpose:** Demonstrates the platform's capability to simulate complex CPS scenarios with sensors, controllers, and monitors, comparing non‑real‑time and real‑time modes.

### 5.3 仿真案例二：虚拟电厂 (VPP) 频率响应 / Case 2: VPP Frequency Response

* **文件**: `frequency_system.*`, `vpp_system.cpp.cpp` (VPP初始化与任务启动部分)
* **目标**: 模拟大规模分布式资源（EV充电桩、储能单元）聚合为虚拟电厂，参与电网一次频率调节，并展示平台在**精细化、大规模并发建模**与**事件驱动优化**方面的核心能力。

* **Files:** `frequency_system.*`, `vpp_system.cpp.cpp` (VPP initialization and task start)
* **Purpose:** Simulates large‑scale distributed resources (EV chargers, storage units) aggregated as a VPP to participate in primary frequency regulation, showcasing fine‑grained large‑scale modeling and event‑driven optimization.

### 5.4 仿真案例三：逻辑保护仿真 / Case 3: Logic Protection

* **文件**: `logic_protection_system.h`, `logic_protection_system.cpp` 以及 `main.cpp` 中相关的初始化与任务启动部分。
* **目标**: 演示平台在模拟电力系统继电保护基本逻辑、信息物理交互、以及复杂故障场景（如断路器拒动、后备保护配合）下的行为。**此案例重点关注保护的逻辑行为和时序配合，不涉及详细的电气定值计算和短路电流分析。**

* **Files:** `logic_protection_system.h`, `logic_protection_system.cpp`, and initialization/startup parts in `main.cpp`.
* **Purpose:** Demonstrates basic protective relay logic, cyber‑physical interactions, and complex fault scenarios (breaker failure, backup coordination). **This case focuses on logical behavior and timing, not detailed electrical settings or fault current analysis.**

### 5.5 仿真统计与结果输出 / Simulation Statistics & Output

* 仿真结束后会统计并输出仿真的**真实物理执行时间**和**峰值内存占用**（通过平台相关的API获取），用于评估仿真平台的性能。

* After simulation, ADN-CPSim reports **actual execution time** and **peak memory usage** via platform APIs to evaluate performance.

## 6. 如何构建与运行 / Build & Run

### 依赖 / Dependencies

* C++20 兼容的编译器 (推荐 GCC 10+ 或 Clang 12+)
* CMake (3.20 或更高版本)
* `spdlog` 库:
    * **Ubuntu/Debian**: `sudo apt install libspdlog-dev`
    * **其他系统或源码安装**: 请参考 `spdlog` 官方文档。
* `pthread` (通常在Linux系统中已具备)

* C++20 compatible compiler (GCC 10+ or Clang 12+ recommended)
* CMake 3.20 or newer
* `spdlog` library:
    * **Ubuntu/Debian:** `sudo apt install libspdlog-dev`
    * **Other systems or source:** see the `spdlog` documentation.
* `pthread` (usually available on Linux)

### 构建步骤 (以Linux为例) / Build Steps (Linux example)

1. 克隆项目:
    ```bash
    git clone <你的项目仓库URL>
    cd <项目目录>
    ```
2. 创建构建目录并进入:
    ```bash
    mkdir build
    cd build
    ```
3. 运行 CMake (指定构建类型，如Release以获得优化性能):
    ```bash
    cmake -DCMAKE_BUILD_TYPE=Release ..
    ```
4. 编译项目:
    ```bash
    make -j$(nproc)
    ```
5. 运行可执行文件 (假设生成 `adn_cpsim`):
    ```bash
    ./adn_cpsim
    ```

1. Clone the project:
    ```bash
    git clone <your repo URL>
    cd <project>
    ```
2. Create and enter a build directory:
    ```bash
    mkdir build
    cd build
    ```
3. Run CMake (specify build type such as Release for optimized performance):
    ```bash
    cmake -DCMAKE_BUILD_TYPE=Release ..
    ```
4. Compile the project:
    ```bash
    make -j$(nproc)
    ```
5. Run the executable (assuming `adn_cpsim`):
    ```bash
    ./adn_cpsim
    ```

更多构建信息请参考根目录下的 `CMakeLists.txt`。

For detailed build configuration, see `CMakeLists.txt` in the project root.

## 7. 预期应用与未来展望 / Roadmap & Future Work

* **主动配电网规划与运行分析**：评估不同控制策略、VPP配置、保护方案在复杂扰动下的性能。
* **CPS算法验证与优化**：为新的分布式控制、优化调度、故障诊断与自愈等算法提供高效的仿真验证平台。
* **教育与研究**：作为学习和研究电力系统CPS建模、事件驱动仿真、C++协程应用的实例。

* **Active distribution network planning and operation analysis:** evaluate control strategies, VPP configurations, and protection schemes under disturbances.
* **CPS algorithm validation and optimization:** provide an efficient testbed for distributed control, optimal scheduling, fault diagnosis, and self‑healing algorithms.
* **Education and research:** serve as an example for CPS modeling, event‑driven simulation, and C++ coroutine usage in power systems.

未来工作可能包括：
* 扩展更丰富的电力设备模型库（如光伏、风机、详细的变压器和线路模型）和CPS应用模型库（如馈线自动化逻辑、微网能量管理）。
* 集成简易的电网潮流计算或动态仿真核心，以提供更精确的物理背景。
* 增强图形化界面和结果可视化功能。
* 支持与外部系统（如实时数字仿真器RTDS/OPAL-RT、SCADA系统原型）通过标准接口（如FMI、HLA、OPC UA）进行联合仿真。
* 进一步优化大规模系统仿真的性能，例如探索分布式仿真。

Future work may include:
* Extending device and CPS model libraries (e.g., PV, wind, detailed transformer and line models; feeder automation, microgrid energy management).
* Integrating simple power‑flow or dynamic simulation cores for more accurate physical backgrounds.
* Enhancing GUIs and result visualization.
* Supporting co‑simulation with external systems (RTDS/OPAL‑RT, SCADA prototypes) via standard interfaces such as FMI, HLA, or OPC UA.
* Further optimizing large‑scale simulations, including exploration of distributed simulation.

## 8. 贡献与许可证 / Contribution & License

欢迎对本项目进行改进和贡献！如果您有任何问题或建议，请通过Issue提出。

Contributions are welcome! Please open issues for questions or suggestions.

本项目旨在促进学术交流与技术验证，采用宽松的MIT许可证。这意味着您可以自由地使用、复制、修改、合并、出版发行、散布、再授权及贩售软件及软件的副本，惟须遵守下列条件：**不得侵犯作者原创权利，不得申请专利、软件著作权；学术研究使用需要在参考文献中列出本项目。否则将依法追究法律责任**。

This project adopts the permissive MIT License to promote academic exchange and technical validation. You may use, copy, modify, merge, publish, distribute, sublicense, and sell copies of the software, provided that **the original authorship is not infringed, no patents or software copyrights are filed, and academic usage cites this project; otherwise legal liability may be pursued**.