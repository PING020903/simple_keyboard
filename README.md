# 简单键盘抽象库 (Simple Keyboard)

轻量级嵌入式按键抽象层，提供扁平按键模型、软件消抖、按 tick 计时的长按/自动重复检测，以及每键独立的函数指针处理回调。支持任意数量的不规则按键布局。当前版本 **v0.4.1**。

## 特性

- **扁平按键模型**：键盘由 N 个相互独立的键组成，每个键通过 `read_raw` 回调读取原始电平，与行列矩阵解耦，天然支持不规则布局。
- **独立消抖组件**：复用门禁工程的 `debounce` 组件，每键状态仅占 1 字节，无硬件依赖。
- **tick 驱动计时**：`skb_tick()` 由时基 tick（如调度器 EVT_TICK）驱动累加按下时长；`skb_poll()` 按扫描周期（如 10ms）采样。时间由系统注入，调用方不传时间参数，时长单位为 tick（tick 到时间的换算由应用定义）。
- **每键函数指针处理回调组**：每个按键挂自己的 `skb_handlers_t`（`on_press` / `on_click` / `on_long_start` / `on_long_hold` / `on_long_up`），直接分配不同处理逻辑；字段为 NULL 即忽略该事件；每键独立 `param`。
- **编译期特性宏**：`SKB_CFG_ENABLE_PRESSED` / `SKB_CFG_ENABLE_CLICK` / `SKB_CFG_ENABLE_LONG` / `SKB_CFG_ENABLE_REPEAT` 可选择性开启，关闭后对应的 handler 字段、检测分支、状态字段全部编译剔除，节省 Flash、RAM 与每 tick CPU。
- **每键可配阈值**：每键独立 `long_ticks`（长按阈值）与 `repeat_ticks`（自动重复周期），或使用全局默认；纯配置驱动。
- **内存优化**：运行时状态不内嵌配置，只保存 `const cfg *` 指针；32 键静态池在 x64 下约 768B。
- **零动态内存**：按键静态池（`SKB_MAX_KEYS` 上限 + 运行期 N），无 malloc。
- **第三方组件**：debounce（消抖）、memguard/GMP（静态内存池）、EventScheduling（事件调度器）、ringBuffer（调度器队列）、c-linked-list（任务链表）。
- **调试输出可关闭**：`DBG_macro.h` 可通过 `DBG_ENABLE=0` 整体关闭，不引入 `snprintf`。

## 重要提示

本项目仅针对裸机环境实现，未实现线程安全保护。在 RTOS 或多线程环境中使用时，需自行添加互斥锁或临界区。

## 快速开始

```c
#include "simple_keyboard.h"

/* 1. 每个按键一个原始输入读取回调（1=按下）*/
static bool read_doorbell(void *ctx) { return GPIO_ReadPin(DOORBELL_PIN); }
static bool read_reset(void *ctx)    { return GPIO_ReadPin(RESET_PIN); }

/* 2. 定义处理回调（函数指针分配不同处理逻辑）*/
static void on_click(uint16_t key_id, uint32_t ticks, void *param) {
    handle_click(key_id);   /* param 为每键独立参数；ticks 为按住时长（tick 数）*/
}
static void on_long_start(uint16_t key_id, uint32_t ticks, void *param) {
    handle_long(key_id);
}

static const skb_handlers_t doorbell_h = { .on_click = on_click };
static const skb_handlers_t reset_h = {
    .on_click      = on_click,
    .on_long_start = on_long_start,
};

/* 3. 定义按键配置（static/const，生命周期须覆盖键盘使用期）
 *    时长单位 = tick（skb_tick 调用次数），tick->时间 换算由应用定义 */
static const skb_key_cfg_t keys[] = {
    { .key_id = 1, .read_raw = read_doorbell, .handlers = &doorbell_h, .param = "doorbell" },
    { .key_id = 2, .read_raw = read_reset,    .long_ticks = 5000, .handlers = &reset_h },
};

static skb_t kb;

/* 4. 由调度器任务驱动：每 tick 计时 + 每 10 tick 扫描 */
void kb_task_on_tick(void) {
    static unsigned div = 0;
    skb_tick(&kb);                 /* 每 tick 计时（press_ticks++ -> LONG/REPEAT）*/
    if (++div >= 10) { div = 0; skb_poll(&kb); }  /* 10 tick 扫描（消抖/边沿）*/
}

int main(void) {
    skb_init(&kb, keys, 2, 500, 0); /* 全局长按 500 tick，不自动重复 */
    /* ... 注册 kb_task_on_tick 到时基（如调度器 EVT_TICK）... */
}
```

### 长按自动重复示例

```c
/* 配置键：长按 1000 tick 后，按住期间每 300 tick 触发一次 on_long_hold */
static void on_long_hold(uint16_t key_id, uint32_t repeat_idx, void *param) {
    do_increment(key_id, repeat_idx);
}
static const skb_handlers_t cfg_h = {
    .on_click      = on_click,
    .on_long_start = on_long_start,
    .on_long_hold  = on_long_hold,
};
static const skb_key_cfg_t cfg_key = {
    .key_id = 3, .read_raw = read_config,
    .long_ticks = 1000, .repeat_ticks = 300,
    .handlers = &cfg_h,
};
```

## 处理回调

| 回调 | 触发条件 | info 参数 |
|------|----------|-----------|
| `on_press` | 消抖确认按下 | 0 |
| `on_click` | 松开且未跨过 long_ticks | hold ticks（按住 tick 数）|
| `on_long_start` | 按住 tick 跨过 long_ticks | hold ticks |
| `on_long_hold` | 长按后每 repeat_ticks 一次 | repeat_idx（序号）|
| `on_long_up` | 长按后松开 | hold ticks |

时长单位一律为 tick（`skb_tick` 调用次数），tick 到时间的换算（如 1 tick = 1ms）由应用定义，库不做单位假设。每个按键的 `handlers` 可为 NULL（忽略该键全部事件）；字段为 NULL 则忽略该事件。分类由模块依据该键 `long_ticks` / `repeat_ticks` 完成，用户无需自行分档。

## 特性宏（编译期裁剪）

| 宏 | 默认 | 关闭后的影响 |
|------|------|------|
| `SKB_CFG_ENABLE_PRESSED` | ON | 不检测/分发按下事件，`on_press` 字段剔除 |
| `SKB_CFG_ENABLE_CLICK` | ON | 不检测/分发短按事件 |
| `SKB_CFG_ENABLE_LONG` | ON | 不检测/分发长按（START/UP），`long_fired` 状态剔除 |
| `SKB_CFG_ENABLE_REPEAT` | ON | 不检测自动重复（需同时开启 LONG）|

CMake 侧对应 `option(SKB_CFG_ENABLE_*)`（如 `-DSKB_CFG_ENABLE_REPEAT=OFF`），宏默认 1 可由 `-D` 覆盖。

## 消抖调优

按键消抖由 `thirdparty/debounce` 组件完成，采用连续 N 次稳定采样才确认的方式。消抖窗口近似为：

```
消抖窗口 ≈ 轮询周期 × DEBOUNCE_SAMPLES
```

- `DEBOUNCE_SAMPLES`：取样次数，定义于 `thirdparty/debounce/debounce.h`（`#ifndef` 包裹，上限 127，默认 3）。
- 轮询周期：`skb_poll` 的调用间隔，由应用决定（如每 10ms 一次）。

示例：10ms 轮询 × 3 次 = 30ms 消抖窗口。若轮询降到 20ms，仍想要约 40ms 窗口，可将取样次数调低到 2 次：

```bash
cmake -B build -G Ninja -DCMAKE_C_FLAGS="-DDEBOUNCE_SAMPLES=2"
```

`DEBOUNCE_SAMPLES` 是全局编译期设置，同一键盘所有按键共用一个消抖窗口。调低取样次数可维持较慢轮询下的消抖窗口，但对机械抖动的过滤能力相应下降，需在响应速度与抗抖之间权衡。

## 构建

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

或在 Windows 上运行 `build.bat`。工程使用 C99 标准（`CMAKE_C_STANDARD 99` / `-std=gnu99`）。

### 调试开关

| 开关 | 默认 | 说明 |
|------|------|------|
| `SKB_DEBUG` (CMake) / `-DSKB_DEBUG` | OFF | 启用 simple_keyboard 组件内部调试打印 |
| `SKB_DEMO_PRINTS=OFF` (CMake) / `-DDBG_ENABLE=0` | ON | 整体关闭 `DBG_macro.h` 打印宏：编译为空操作，不引入 `<stdio.h>` / `snprintf` |
| `SKB_ARG_CHECK=OFF` (CMake) / `-DSKB_ARG_CHECK_ENABLE=0` | ON | 关闭 API 参数校验，减小代码与运行时开销；关闭后非法入参将导致未定义行为 |

## API 参考

| 函数 | 说明 |
|------|------|
| `skb_init(kb, cfg, n, global_long_ticks, global_repeat_ticks)` | 初始化并注册 N 个按键与全局触发配置 |
| `skb_poll(kb)` | 按键扫描：读原始电平 → 消抖 → 边沿 → 调 on_press / on_click / on_long_up |
| `skb_tick(kb)` | 按键计时：按住键 press_ticks++ → 调 on_long_start / on_long_hold（每 tick 时基）|
| `skb_hold_ticks(kb, key_id)` | 查询某键当前按住 tick 数 |
| `skb_is_pressed(kb, key_id)` | 查询某键当前是否按下 |

返回值：多数函数返回 `SKB_OK` 表示成功，其他为错误码（`SKB_ERR_ARG`、`SKB_ERR_NOT_READY`、`SKB_ERR_TOO_MANY`、`SKB_ERR_NOT_FOUND`）。

完整 API 见 [simple_keyboard.h](include/simple_keyboard.h)。

## 演示：真实运行时仿真

`main.c` 忠实复刻门禁工程的运行时模型，按真实时间运行（约 10s）：

| 角色 | 实现 | 说明 |
|------|------|------|
| 模拟 SysTick | 时钟线程 `Sleep(1); g_tick_ms++` | 唯一系统时间源（`timeBeginPeriod(1)` 保证 1ms 精度）|
| 调度器运行时 | 调度线程调用 `EventSchedul_MainLoop(ctx)` | pull → idle 钩子 → 派发 |
| idle 钩子 | `evtSchedul_idle` 注册为 sleepMethod | 检测 `g_tick_ms` 前进即广播 `EVT_TICK` |
| 键盘任务 | `kb_task` 收 EVT_TICK | `skb_tick`(1ms) + 10 tick 分频 `skb_poll`(10ms) + 场景推进 |
| 业务任务 | `biz_task` 收按键事件 | 各键 handler 经 `setEventToTask` 投递，调度器统一派发 |

演示输出中 `[kb]` 行来自键盘层每键 handler，`[sched]` 行来自调度器派发的业务任务。完整链路：按键 → 消抖/计时 → handler → 调度器队列 → 任务。

## 第三方组件

| 组件 | 目录 | 用途 |
|------|------|------|
| debounce | `thirdparty/debounce` | 按键软件消抖（每键 1 字节，纯数据零依赖）|
| memguard (GMP) | `thirdparty/memguard` | 静态内存池，调度器上下文分配器（禁 malloc）|
| EventScheduling | `thirdparty/EventScheduling` | 事件调度器：任务注册、事件投递、MainLoop / TmosPoll |
| ringBuffer | `thirdparty/ringBuffer` | 调度器内部事件队列（不定长数据项）；[修复] `_calc_count` 回绕计数 bug |
| c-linked-list | `thirdparty/c-linked-list` | 任务链表管理（头文件库）|

## 注意事项

1. 使用前必须调用 `skb_init` 完成初始化，`read_raw` 不可为空。
2. `cfg` 数组须 `static/const` 且生命周期覆盖键盘使用期（运行时状态只存指针，不复制）。
3. `skb_tick` 由时基 tick 驱动（精度即时基精度）；`skb_poll` 按扫描周期调用，决定消抖采样率。
4. 每个按键 `key_id` 需唯一；`read_raw` 返回 true=按下；`handlers` 可为 NULL。
5. `repeat_ticks` 只在跨过 `long_ticks`（触发 on_long_start）后生效。
6. 中断服务程序中需注意重入问题；多线程使用时需自行添加同步机制。

## 许可证

本项目为学习用途，可根据需要自由使用和修改。
