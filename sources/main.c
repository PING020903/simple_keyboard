#include "DBG_macro.h"
#include "simple_keyboard.h"
#include "debounce.h"
#include "EventSchedul.h"
#include "memGroundP.h"
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>

#if DBG_ENABLE
char __DBG_string[DBG_DEFAULT_BUFFER_LEN] = {0};
#endif

/* ============================================================
 * 按键事件 -> 调度器事件号编码（演示自定）：
 *   bit15:13 = 事件（PRESSED=0/CLICK=1/LONG_START=2/LONG_HOLD=3/LONG_UP=4）
 *   bit12:0  = 键号 + 1（避开调度器保留的 0x0000 / 0xffff）
 * ============================================================ */
#define SKB_EVT_GET_KEYCODE(_evt) ((uint16_t)(((_evt) & 0x1FFFu) - 1u))
#define SKB_EVT_GET_EVENT(_evt)   ((uint8_t)(((_evt) >> 13) & 0x07u))
#define SKB_EVT_MAKE(_code, _event) \
    (EventSchedul_EventId)((((_code) & 0x1FFFu) + 1u) | ((uint16_t)((_event) & 0x07u) << 13))

enum
{
    EVT_PRESSED   = 0,
    EVT_CLICK     = 1,
    EVT_LONG_START = 2,
    EVT_LONG_HOLD = 3,
    EVT_LONG_UP   = 4,
};

static EventSchedul_Context  *g_ctx     = NULL;
static EventSchedul_TaskNode *g_kb_task = NULL;
static EventSchedul_TaskNode *g_biz_task = NULL;

/* ============================================================
 * GMP 静态内存池：调度器上下文分配器
 * ============================================================ */
#define GMP_POOL_SIZE 2048
static uint8_t g_gmp_mem[GMP_POOL_SIZE];
static mgp_t   g_gmp_pool = NULL;

static void *gmp_adapt_malloc(size_t size)
{
    return mgp_malloc(g_gmp_pool, size);
}

static void gmp_adapt_free(void *ptr)
{
    mgp_free(g_gmp_pool, ptr);
}

/* ============================================================
 * 模拟 SysTick 1ms 时基（时钟线程，相当于 SysTick 中断）
 *   g_tick_ms 是唯一系统时间源；调度器 idle 钩子检测其前进
 *   即广播 EVT_TICK，键盘计时/场景推进都由它驱动。
 * ============================================================ */
static volatile uint32_t g_tick_ms = 0;
static volatile bool     g_stop    = false;

static DWORD WINAPI systick_thread(LPVOID param)
{
    (void)param;
    while (!g_stop)
    {
        Sleep(1);       /* 真实 1ms（已 timeBeginPeriod(1)）*/
        g_tick_ms++;    /* 模拟 SysTick_Handler */
    }
    return 0;
}

/* ============================================================
 * 时基广播（移植门禁 tickBroadcast）：向订阅任务投递 EVT_TICK
 * ============================================================ */
#define TICKBROADCAST_MAX_TASKS 8
#define EVT_TICK ((EventSchedul_EventId)1)

static EventSchedul_TaskNode *g_tick_tasks[TICKBROADCAST_MAX_TASKS];
static uint8_t g_tick_task_cnt = 0;

static void tickBroadcast_register(EventSchedul_TaskNode *task)
{
    if (g_tick_task_cnt < TICKBROADCAST_MAX_TASKS && task != NULL)
        g_tick_tasks[g_tick_task_cnt++] = task;
}

static void tickBroadcast_tick(void)
{
    for (uint8_t i = 0; i < g_tick_task_cnt; i++)
        EventSchedul_setEventToTask(g_ctx, g_tick_tasks[i], EVT_TICK);
}

/* ============================================================
 * 调度器 idle 钩子（忠实复刻门禁 board_bus.c evtSchedul_idle）：
 *   检测到 g_tick_ms 前进（每真实 1ms 一次）即广播 EVT_TICK。
 * ============================================================ */
static void evtSchedul_idle(void)
{
    static uint32_t last_tick = 0;
    const uint32_t now = g_tick_ms;

    if (now != last_tick)
    {
        last_tick = now;
        tickBroadcast_tick();
    }
    Sleep(0);   /* 让出 CPU，避免调度线程忙等占用 */
}

/* ============================================================
 * 模拟按键输入：6 个相互独立的键（不规则布局，无矩阵关系）
 * ============================================================ */
#define SIM_KEY_CNT 6
static bool sim_flag[SIM_KEY_CNT] = {false};

static bool sim_key_read(void *ctx)
{
    size_t ch = (size_t)ctx;
    return (ch < SIM_KEY_CNT) ? sim_flag[ch] : false;
}

enum
{
    KEY_DOORBELL = 1,
    KEY_UNLOCK   = 2,
    KEY_CONFIG   = 3,
    KEY_RESET    = 4,
    KEY_A        = 5,
    KEY_B        = 6,
};

/* ============================================================
 * 每键处理回调：打印事件并编码投递调度器（业务任务统一派发）
 *   param 为该键配置的独立参数（演示用标签字符串）
 * ============================================================ */
static const char *g_phase = "?";
static skb_t kb;

#if DBG_ENABLE
static const char *event_name(uint8_t e)
{
    switch (e)
    {
    case EVT_PRESSED:    return "PRESSED";
    case EVT_CLICK:      return "CLICK";
    case EVT_LONG_START: return "LONG_START";
    case EVT_LONG_HOLD:  return "LONG_HOLD";
    case EVT_LONG_UP:    return "LONG_UP";
    }
    return "?";
}
#endif

#if SKB_CFG_ENABLE_PRESSED
static void kb_on_press(uint16_t key_id, uint32_t info, void *param)
{
    const char *label = (param != NULL) ? (const char *)param : "?";
    (void)info; (void)label; /* DBG_ENABLE=0 时打印为空操作 */
    DEBUG_PRINT("[%s][kb] key=%d(%s) PRESSED", g_phase, key_id, label);
    EventSchedul_setEventToTask(g_ctx, g_biz_task, SKB_EVT_MAKE(key_id, EVT_PRESSED));
}
#endif

#if SKB_CFG_ENABLE_CLICK
static void kb_on_click(uint16_t key_id, uint32_t info, void *param)
{
    const char *label = (param != NULL) ? (const char *)param : "?";
    (void)label; /* DBG_ENABLE=0 时打印为空操作 */
    DEBUG_PRINT("[%s][kb] key=%d(%s) CLICK hold_ticks=%u", g_phase, key_id, label, info);
    EventSchedul_setEventToTask(g_ctx, g_biz_task, SKB_EVT_MAKE(key_id, EVT_CLICK));
}
#endif

#if SKB_CFG_ENABLE_LONG
static void kb_on_long_start(uint16_t key_id, uint32_t info, void *param)
{
    (void)param;
    DEBUG_PRINT("[%s][kb] key=%d LONG_START hold_ticks=%u", g_phase, key_id, info);
    EventSchedul_setEventToTask(g_ctx, g_biz_task, SKB_EVT_MAKE(key_id, EVT_LONG_START));
}
#endif

#if SKB_LONG_REPEAT_ENABLED
static void kb_on_long_hold(uint16_t key_id, uint32_t info, void *param)
{
    (void)param;
    DEBUG_PRINT("[%s][kb] key=%d LONG_HOLD#%u hold_ticks=%u",
                g_phase, key_id, info, skb_hold_ticks(&kb, key_id));
    EventSchedul_setEventToTask(g_ctx, g_biz_task, SKB_EVT_MAKE(key_id, EVT_LONG_HOLD));
}
#endif

#if SKB_CFG_ENABLE_LONG
static void kb_on_long_up(uint16_t key_id, uint32_t info, void *param)
{
    (void)param;
    DEBUG_PRINT("[%s][kb] key=%d LONG_UP hold_ticks=%u", g_phase, key_id, info);
    EventSchedul_setEventToTask(g_ctx, g_biz_task, SKB_EVT_MAKE(key_id, EVT_LONG_UP));
}
#endif

/* 每键处理回调组：不同按键挂不同组合，演示"分配不同处理逻辑" */
static const skb_handlers_t doorbell_handlers = {
#if SKB_CFG_ENABLE_PRESSED
    .on_press      = kb_on_press,
#endif
#if SKB_CFG_ENABLE_CLICK
    .on_click      = kb_on_click,
#endif
};

static const skb_handlers_t unlock_handlers = {
#if SKB_CFG_ENABLE_CLICK
    .on_click      = kb_on_click,
#endif
#if SKB_CFG_ENABLE_LONG
    .on_long_start = kb_on_long_start,
    .on_long_up    = kb_on_long_up,
#endif
};

static const skb_handlers_t config_handlers = {
#if SKB_CFG_ENABLE_CLICK
    .on_click      = kb_on_click,
#endif
#if SKB_CFG_ENABLE_LONG
    .on_long_start = kb_on_long_start,
#endif
#if SKB_LONG_REPEAT_ENABLED
    .on_long_hold  = kb_on_long_hold,
#endif
#if SKB_CFG_ENABLE_LONG
    .on_long_up    = kb_on_long_up,
#endif
};

static const skb_handlers_t reset_handlers = {
#if SKB_CFG_ENABLE_CLICK
    .on_click      = kb_on_click,
#endif
#if SKB_CFG_ENABLE_LONG
    .on_long_start = kb_on_long_start,
    .on_long_up    = kb_on_long_up,
#endif
};

static const skb_handlers_t keya_handlers = {
#if SKB_CFG_ENABLE_PRESSED
    .on_press      = kb_on_press,
#endif
#if SKB_CFG_ENABLE_CLICK
    .on_click      = kb_on_click,
#endif
};

static const skb_key_cfg_t key_cfgs[] = {
    { .key_id = KEY_DOORBELL, .read_raw = sim_key_read, .ctx = (void *)(size_t)0,
      .param = "doorbell", .handlers = &doorbell_handlers },
    { .key_id = KEY_UNLOCK,   .read_raw = sim_key_read, .ctx = (void *)(size_t)1,
      .param = "unlock",   .handlers = &unlock_handlers },
    { .key_id = KEY_CONFIG,   .read_raw = sim_key_read, .ctx = (void *)(size_t)2,
      .long_ticks = 1000, .repeat_ticks = 300, .handlers = &config_handlers },
    { .key_id = KEY_RESET,    .read_raw = sim_key_read, .ctx = (void *)(size_t)3,
      .long_ticks = 5000, .handlers = &reset_handlers },
    { .key_id = KEY_A,        .read_raw = sim_key_read, .ctx = (void *)(size_t)4,
      .param = "keyA",    .handlers = &keya_handlers },
    { .key_id = KEY_B,        .read_raw = sim_key_read, .ctx = (void *)(size_t)5 },
};
#define KEY_COUNT (sizeof(key_cfgs) / sizeof(key_cfgs[0]))

/* ============================================================
 * 业务任务：接收经调度器派发的按键事件
 * ============================================================ */
static void biz_task(EventSchedul_EventId evt, void *arg)
{
    (void)arg;
    const uint16_t key_id = SKB_EVT_GET_KEYCODE(evt);
    const uint8_t event = SKB_EVT_GET_EVENT(evt);
    (void)key_id; (void)event; /* DBG_ENABLE=0 时打印为空操作 */
#if DBG_ENABLE
    DEBUG_PRINT("[%s][sched] key=%d event=%s", g_phase, key_id, event_name(event));
#endif
}

/* ============================================================
 * 场景脚本（在键盘任务上下文、按真实 1ms tick 推进）
 *   每个测试是一张步骤表：相对毫秒时刻 -> 模拟按键按下/释放
 * ============================================================ */
typedef struct
{
    uint32_t at_ms; /* 相对测试起点的毫秒时刻 */
    uint8_t  ch;    /* 模拟按键通道 */
    bool     level; /* true=按下 false=松开 */
} sim_step_t;

typedef struct
{
    const char *tag;          /* 测试标记 */
    uint32_t duration_ms;     /* 测试总时长 */
    const sim_step_t *steps;  /* 步骤表 */
    uint32_t step_count;
} sim_test_t;

static const sim_step_t t1_steps[] = { {0, 0, true}, {100, 0, false} };
static const sim_step_t t2_steps[] = { {0, 1, true}, {800, 1, false} };
static const sim_step_t t3_steps[] = { {0, 2, true}, {1700, 2, false} };
static const sim_step_t t4a_steps[] = { {0, 3, true}, {800, 3, false} };
static const sim_step_t t4b_steps[] = { {0, 3, true}, {5500, 3, false} };
static const sim_step_t t5_steps[] = { {0, 4, true}, {10, 4, false} };
static const sim_step_t t6_steps[] = {
    {0, 0, true}, {0, 1, true}, {0, 2, true},
    {100, 0, false}, {100, 1, false}, {100, 2, false},
};

static const sim_test_t g_tests[] = {
    {"T1 short press",        160,  t1_steps,  2},
    {"T2 long press",         860,  t2_steps,  2},
    {"T3 auto repeat",        1760, t3_steps,  2},
    {"T4a key threshold 800ms", 860, t4a_steps, 2},
    {"T4b key threshold 5500ms",5560, t4b_steps, 2},
    {"T5 debounce glitch",    70,   t5_steps,  2},
    {"T6 scheduler pipeline", 160,  t6_steps,  6},
};
#define TEST_COUNT (sizeof(g_tests) / sizeof(g_tests[0]))

static uint32_t g_test_idx = 0;
static uint32_t g_test_ms  = 0;
static volatile bool g_done = false;

/* 每 1ms 由键盘任务调用一次：应用步骤、推进测试、打印结果标记 */
static void scenario_step(void)
{
    if (g_test_idx >= TEST_COUNT)
    {
        if (!g_done)
        {
            g_done = true;
            DEBUG_PRINT("========================================");
            DEBUG_PRINT("All tests completed!");
            DEBUG_PRINT("========================================");
        }
        return;
    }

    const sim_test_t *t = &g_tests[g_test_idx];

    if (g_test_ms == 0)
    {
        g_phase = t->tag;
        DEBUG_PRINT("=== %s ===", t->tag);
    }

    for (uint32_t i = 0; i < t->step_count; i++)
    {
        if (t->steps[i].at_ms == g_test_ms)
            sim_flag[t->steps[i].ch] = t->steps[i].level;
    }

    g_test_ms++;
    if (g_test_ms >= t->duration_ms)
    {
        g_test_idx++;
        g_test_ms = 0;
    }
}

/* ============================================================
 * 键盘任务：收 EVT_TICK -> 1ms 计时 + 10ms 扫描 + 场景推进
 * ============================================================ */
#define POLL_DIV 10   /* 每 10 个 tick 扫描一次（10ms）*/
static uint32_t g_poll_div = 0;

static void kb_task(EventSchedul_EventId evt, void *arg)
{
    (void)arg;
    if (evt != EVT_TICK)
        return;

    skb_tick(&kb);                 /* 每 tick 计时：press_ticks++ -> LONG/REPEAT */
    if (++g_poll_div >= POLL_DIV)
    {
        g_poll_div = 0;
        skb_poll(&kb);             /* 10ms 扫描：读原始 -> 消抖 -> 边沿 */
    }
    scenario_step();               /* 推进场景脚本 */
}

/* ============================================================
 * 调度器线程：EventSchedul_MainLoop 真正运行
 *   pull -> evtSchedul_idle（广播 EVT_TICK）-> 派发任务
 * ============================================================ */
static DWORD WINAPI scheduler_thread(LPVOID param)
{
    (void)param;
    EventSchedul_MainLoop(g_ctx);   /* 无限循环，永不返回 */
    return 0;
}

/* ============================================================
 * main：初始化 -> 启动时钟/调度器线程 -> 等场景跑完
 * ============================================================ */
int main(void)
{
    DEBUG_PRINT("========================================");
    DEBUG_PRINT("Simple Keyboard v0.4.1 Runtime Simulation");
    DEBUG_PRINT("(scheduler MainLoop + 1ms SysTick + EVT_TICK)");
    DEBUG_PRINT("========================================");

    /* GMP 静态内存池 -> 调度器上下文分配器 */
    g_gmp_pool = mgp_create_with_pool(g_gmp_mem, GMP_POOL_SIZE);
    if (g_gmp_pool == NULL)
    {
        DEBUG_PRINT("mgp_create_with_pool: FAILED");
        return -1;
    }
    DEBUG_PRINT("mgp_create_with_pool: OK (pool=%uB)",
                (unsigned)mgp_canAllocMaxSize(g_gmp_pool));

    g_ctx = EventSchedul_Create(&(EventSchedul_Allocator){gmp_adapt_malloc, gmp_adapt_free});
    if (g_ctx == NULL)
    {
        DEBUG_PRINT("EventSchedul_Create: FAILED");
        return -1;
    }
    DEBUG_PRINT("EventSchedul_Create: OK");

    /* 注册键盘任务（仅收 EVT_TICK）+ 业务任务（收按键事件）*/
    EventSchedul_TaskNode cfg = {0};
    cfg.pTaskFunc       = kb_task;
    cfg.info.eventStart = EVT_TICK;
    cfg.info.eventEnd   = EVT_TICK + 1;
    g_kb_task = EventSchedul_TaskRegister(g_ctx, &cfg);
    tickBroadcast_register(g_kb_task);

    cfg = (EventSchedul_TaskNode){0};
    cfg.pTaskFunc       = biz_task;
    cfg.info.eventStart = 0x0001;
    cfg.info.eventEnd   = 0xffff;
    g_biz_task = EventSchedul_TaskRegister(g_ctx, &cfg);

    /* 注册调度器 idle 钩子（广播 EVT_TICK）*/
    EventSchedul_RegSleepMethod(g_ctx, evtSchedul_idle);

    /* 初始化键盘：6 个独立按键，全局 long_ticks=500，全局不重复
     * （本演示 1 tick = 1ms，故 long_ticks=500 即 500ms）*/
    skb_err_t err = skb_init(&kb, key_cfgs, KEY_COUNT, 500, 0);
    DEBUG_PRINT("skb_init: %d (expect 0), keys=%u", err, (unsigned)KEY_COUNT);
    (void)err; /* DBG_ENABLE=0 时打印为空操作 */

    /* 提高 Sleep 精度到 1ms（真实时间仿真）*/
    timeBeginPeriod(1);

    /* 启动时钟线程（SysTick）与调度器线程（MainLoop）*/
    CreateThread(NULL, 0, systick_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, scheduler_thread, NULL, 0, NULL);

    /* 主线程等待场景状态机跑完全部测试（约 10s 真实时间）*/
    while (!g_done)
        Sleep(10);

    g_stop = true;
    Sleep(50);          /* 让时钟线程退出 */
    timeEndPeriod(1);
    return 0;
}
