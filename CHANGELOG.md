# 更新日志 (CHANGELOG)

本文档记录项目的所有重要变更。

---

## [v0.4.1] - 2026-08-10

### 变更

- **时长单位改为 tick**：库的计时单位是 `skb_tick` 调用次数，不假设 ms/s；命名统一去掉 `_ms` 后缀
  - `skb_key_cfg_t.long_ms/repeat_ms` → `long_ticks/repeat_ticks`
  - `skb_t.global_long_ms/global_repeat_ms` → `global_long_ticks/global_repeat_ticks`；`skb_init` 参数同步
  - 内部 `press_ms/next_repeat_ms` → `press_ticks/next_repeat_ticks`
  - `skb_hold_ms(kb, key_id)` → `skb_hold_ticks(kb, key_id)`
  - handler 的 `info` 语义：`on_click`/`on_long_start`/`on_long_up` 传 hold ticks（`press_ticks`，`skb_tick` 次数），`on_long_hold` 传 `repeat_idx`（序号），`on_press` 传 0
- tick→时间 的换算（如 1 tick = 1ms）由应用定义，库不做单位假设

---

## [v0.4.0] - 2026-08-10

### 新增

- **每键函数指针处理回调组**：每个按键配置挂 `skb_handlers_t`（on_press/on_click/on_long_start/on_long_hold/on_long_up），直接分配不同处理逻辑；NULL=忽略该事件；每键独立 `param`
- **编译期特性宏**：`SKB_CFG_ENABLE_PRESSED/CLICK/LONG/REPEAT` 选择性开启，关闭后对应 handler 字段、检测分支、状态字段全部编译剔除（省 Flash/RAM/每 tick CPU）；CMake 暴露 `option()`（如 `-DSKB_CFG_ENABLE_REPEAT=OFF`）
- **内存优化**：运行时状态不再内嵌配置，只存 `const cfg*` 指针（32 键池 x64 2KB→768B，省 ~60%）；阈值改 `uint16_t`（上限 65.5s）；`param` 与 `ctx` 分开保留（每键独立）
- **C99 标准**：`set(CMAKE_C_STANDARD 99)` + `-std=gnu99`（与 .clangd、门禁工程一致）
- **ringBuffer 修复**：`_calc_count` 回绕分支 `wr+depth-rd` → `wr+2*depth-rd`（原 bug 在回绕状态下把满队列算成 0，导致调度器事件丢失与重复弹出；已在本工程 vendor 副本修复并标注，上游 `F:\cmake_study\ringBuffer` 同 bug 待同步）

### 变更

- 分发模型：弃用 v0.3.0 的枚举分类（单回调+事件枚举 switch），改为**函数指针处理回调组**——用户无需分档、无 switch
- `skb_init(kb, cfg, n, global_long_ms, global_repeat_ms)`：去掉 cb/cb_param（改每键 handlers+param）
- `cfg` 改为只读配置（`const skb_key_cfg_t *`），删去 `skb_set_longpress/set_repeat`（纯配置驱动）
- 演示 `main.c`：每键挂不同 handlers 组合，handlers 编码投递调度器 `biz_task`

---

## [v0.2.1] - 2026-08-10

### 新增

- **tick 驱动计时**：`skb_poll(kb)` 拆分为扫描（读原始→消抖→边沿）+ `skb_tick(kb)` 1ms 计时（press_ms++ → LONG/REPEAT），彻底去掉手传 `dt` 时间参数；`press_ms` 由模块内部 tick 计数给出，时间由系统（调度器 EVT_TICK）注入
- **真实运行时仿真演示**：`main.c` 忠实复刻门禁运行时——模拟 SysTick 时钟线程（真实 1ms）+ 调度线程运行 `EventSchedul_MainLoop` + idle 钩子广播 EVT_TICK + 键盘任务 1ms 计时/10ms 扫描 + 业务任务经调度器派发；场景按真实时间脚本驱动（约 10s 跑完）
- **调度器逐 ms 调试打印抑制**：`EventSchedul.c` 单独编译 `DBG_ENABLE=0`（EVT_TICK 每 ms 入队不再刷屏），不改 vendor 源码
- **winmm 链接**：`timeBeginPeriod(1)` 提高 `Sleep` 精度到 1ms

### 变更

- `skb_poll` 签名：`skb_poll(kb, dt_ms)` → `skb_poll(kb)` + `skb_tick(kb)`
- 演示输出增加 `[sched]` 业务任务派发行，展示完整"按键→调度器→任务"链路
- 测试场景：新增 T6 全链路（3 键经调度器按序派发），替换原"调度器缓冲"（真实 MainLoop 下事件即时派发）

---

## [v0.2.0] - 2026-08-10

### 新增

- **扁平按键模型**：废弃矩阵（rows/cols、raw 位图、fsm[row][col]、key_map），改为 N 个相互独立的键，每个键独立 `read_raw` 回调读取原始电平，支持任意数量的不规则按键布局
- **时间参数分发回调**：模块只负责消抖 + 按住计时，把 `(键号, 时机, 按下时长)` 交给用户回调（`SKB_MOMENT_PRESS/LONG_CROSS/REPEAT/RELEASE`），短按/长按/超长按等分档完全由用户按时长决定
- **每键可配阈值**：每键独立 `long_ms`/`repeat_ms`，支持长按自动重复（含追补），运行期可按 key_id 覆盖
- **debounce 独立组件**：新增 `thirdparty/debounce`（门禁工程组件，每键 1 字节，纯数据零依赖）
- **memguard (GMP)**：新增 `thirdparty/memguard` 静态内存池，调度器上下文分配器替换 libc malloc
- **测试场景**：短按、长按（LONG_CROSS）、自动重复（多次 REPEAT）、每键独立阈值、消抖滤毛刺、调度器事件缓冲

### 变更

- `skb_poll(kb, dt_ms)` 取代原 `skb_scan`/`skb_process`/`skb_get_event`（事件改为回调直发）
- `skb_init` 参数改为：按键配置数组 + 全局长按/重复阈值 + 分发回调
- v0.1.0 的矩阵键盘 API（skb_scan/skb_set_key_map 等）移除

---

## [v0.1.0] - 2026-08-10

### 功能

- 矩阵扫描：逐行驱动、逐列读取，`raw` 位图记录扫描结果
- 软件消抖：按下/释放两侧均连续 N 次扫描一致才确认，滤除机械抖动
- 按键状态机：空闲 → 按下确认 → 按下 → 长按 → 释放确认 → 空闲
- 长按检测：`skb_set_hold_time` 配置长按阈值，触发 `HOLD` 事件（默认禁用）
- 键值映射：`skb_set_key_map` 设置映射表，未命中使用默认编号 `row*cols+col`
- FIFO 事件队列：`skb_get_event` 取走按下/长按/释放事件
- GPIO 抽象回调：`set_row` / `get_col` 单一移植点
- 零动态内存：全部状态静态分配
- 调试宏支持（`DBG_macro.h`，`DBG_ENABLE` 可整体关闭）

### 构建

- CMake 工程（`include` / `sources` 目录结构），选项：`SKB_DEBUG`、`SKB_DEMO_PRINTS`、`SKB_ARG_CHECK`
- Windows 一键构建脚本 `build.bat`
- `main.c` 演示程序：模拟 4x4 矩阵键盘，覆盖按下/释放、长按、抖动过滤、键值映射、多键同时按下等场景
