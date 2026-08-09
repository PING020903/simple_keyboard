#pragma once
#ifndef _SIMPLE_KEYBOARD_H_
#define _SIMPLE_KEYBOARD_H_

#include <stdint.h>
#include <stdbool.h>
#include "debounce.h"

/* 参数校验开关（含指针 NULL 检查与配置校验）：
 * 默认 1，拦截 NULL 指针及非法配置；
 * 置 0（-DSKB_ARG_CHECK_ENABLE=0）时全部校验编译为空，
 * 视为调用者完全可信，以换取最小代码与运行时开销。 */
#ifndef SKB_ARG_CHECK_ENABLE
#define SKB_ARG_CHECK_ENABLE 1
#endif

/* 按键数量上限（静态池大小）：
 * 运行期实际注册数量 N 由 skb_init 的 cfg 数组长度决定，N <= SKB_MAX_KEYS */
#ifndef SKB_MAX_KEYS
#define SKB_MAX_KEYS 32
#endif

/* ============================================================
 * 特性开关（编译期选择性开启，减小未用功能的 Flash/RAM/每 tick CPU）：
 *   SKB_CFG_ENABLE_PRESSED  : 按下沿事件（on_press）
 *   SKB_CFG_ENABLE_CLICK    : 短按事件（on_click）
 *   SKB_CFG_ENABLE_LONG     : 长按事件（on_long_start / on_long_up）
 *   SKB_CFG_ENABLE_REPEAT   : 长按自动重复（on_long_hold）
 * 关闭后对应 handler 字段、检测分支与状态字段全部编译剔除。
 * ============================================================ */
#ifndef SKB_CFG_ENABLE_PRESSED
#define SKB_CFG_ENABLE_PRESSED 1
#endif
#ifndef SKB_CFG_ENABLE_CLICK
#define SKB_CFG_ENABLE_CLICK   1
#endif
#ifndef SKB_CFG_ENABLE_LONG
#define SKB_CFG_ENABLE_LONG    1
#endif
#ifndef SKB_CFG_ENABLE_REPEAT
#define SKB_CFG_ENABLE_REPEAT  1
#endif

/* 长按自动重复（on_long_hold）需要同时开启 LONG 与 REPEAT */
#if SKB_CFG_ENABLE_LONG && SKB_CFG_ENABLE_REPEAT
#define SKB_LONG_REPEAT_ENABLED 1
#else
#define SKB_LONG_REPEAT_ENABLED 0
#endif

/* 错误码 */
typedef enum
{
    SKB_OK = 0,
    SKB_ERR_FAIL,
    SKB_ERR_ARG,          /* 参数错误 */
    SKB_ERR_NOT_READY,    /* 未初始化 */
    SKB_ERR_TOO_MANY,     /* 按键数量超出 SKB_MAX_KEYS */
    SKB_ERR_NOT_FOUND,    /* 未找到指定 key_id */
} skb_err_t;

/* 原始电平读取回调：读取某个按键的原始输入（1=按下），由调用方实现。
 * 每个按键独立一个回调，与键盘布局无关，天然支持不规则按键排布。 */
typedef bool (*skb_read_fn_t)(void *ctx);

/* 按键处理回调（统一签名）：
 *   key_id : 键号
 *   info   : 附加信息，语义随字段而定（按下=0 / 短按=hold ticks /
 *            长按开始=hold ticks / 长按持续=repeat_idx / 长按松开=hold ticks）
 *   param  : 该键配置的 param（每键独立） */
typedef void (*skb_handler_fn)(uint16_t key_id, uint32_t info, void *param);

/* 每键处理回调组：字段按特性开关编译，NULL = 不处理该事件 */
typedef struct
{
#if SKB_CFG_ENABLE_PRESSED
    skb_handler_fn on_press;      /* 按下沿（info=0）*/
#endif
#if SKB_CFG_ENABLE_CLICK
    skb_handler_fn on_click;      /* 短按（info=hold ticks）*/
#endif
#if SKB_CFG_ENABLE_LONG
    skb_handler_fn on_long_start; /* 长按开始（info=hold ticks）*/
#endif
#if SKB_LONG_REPEAT_ENABLED
    skb_handler_fn on_long_hold;  /* 长按持续（info=repeat_idx）*/
#endif
#if SKB_CFG_ENABLE_LONG
    skb_handler_fn on_long_up;    /* 长按松开（info=hold ticks）*/
#endif
} skb_handlers_t;

/* 单个按键配置（通常 static/const 存放在 ROM，须与键盘同生命周期）*/
typedef struct
{
    skb_read_fn_t read_raw;            /* 该键原始输入来源 */
    const skb_handlers_t *handlers;    /* 该键处理回调组（NULL=忽略该键事件）*/
    void *ctx;                         /* 传给 read_raw 的上下文 */
    void *param;                       /* 传给该键 handler 的参数（每键独立）*/
    uint16_t key_id;                   /* 用户自定义键号（任意编号，需唯一）*/
    uint16_t long_ticks;               /* 长按阈值（0=用全局默认，上限 65535 tick）*/
    uint16_t repeat_ticks;             /* 长按自动重复周期（0=用全局默认；全局为 0 则不重复）*/
} skb_key_cfg_t;

/* 单个按键运行时状态（不内嵌配置，指向用户 cfg）*/
typedef struct
{
    const skb_key_cfg_t *cfg;   /* 指向用户配置（const/ROM）*/
    uint32_t press_ticks;       /* 本次按下累计 tick 数 */
#if SKB_LONG_REPEAT_ENABLED
    uint32_t next_repeat_ticks; /* 下一次重复触发点 */
    uint16_t repeat_idx;        /* 重复序号 */
#endif
    debounce_t db;              /* 消抖状态（debounce 组件，1 字节）*/
    uint8_t pressed;            /* 当前是否处于按下状态（消抖确认后）*/
#if SKB_CFG_ENABLE_LONG
    uint8_t long_fired;         /* 本次按下是否已触发过长按开始 */
#endif
} skb_key_t;

/* 键盘控制结构（静态池，零动态内存） */
typedef struct
{
    skb_key_t keys[SKB_MAX_KEYS]; /* 按键静态池 */
    uint16_t key_count;           /* 实际注册的按键数（N）*/
    uint16_t global_long_ticks;   /* 全局长按阈值（tick 数）*/
    uint16_t global_repeat_ticks; /* 全局长按重复周期（0=不重复）*/
    bool initialized;             /* 是否已初始化 */
} skb_t;

/**
 * @brief 初始化键盘：注册 N 个按键与全局触发配置
 * @note  时长单位为 tick（skb_tick 调用次数），由应用定义 tick->时间映射
 * @param kb                   控制结构指针
 * @param cfg                  按键配置数组（static/const，须与键盘同生命周期）
 * @param n                    按键数量（1~SKB_MAX_KEYS）
 * @param global_long_ticks    全局长按阈值（每键可单独覆盖，0=禁用长按）
 * @param global_repeat_ticks  全局长按重复周期（0=不重复）
 * @return SKB_OK 成功，否则错误码
 */
skb_err_t skb_init(skb_t *kb, const skb_key_cfg_t *cfg, uint16_t n,
                   uint16_t global_long_ticks, uint16_t global_repeat_ticks);

/**
 * @brief 按键扫描：读取各键原始电平并消抖，检测按下/松开边沿
 * @note  按扫描周期（如每 10 tick）调用；按下沿调 on_press，
 *        松开沿按是否已长按调 on_long_up 或 on_click
 * @param kb 控制结构指针
 * @return SKB_OK 成功，否则错误码
 */
skb_err_t skb_poll(skb_t *kb);

/**
 * @brief 按键计时：对处于按下状态的键累加按住 tick，跨过阈值调长按回调
 * @note  每个时基 tick 调用一次（由调度器 EVT_TICK 驱动），press_ticks 由模块内部计数给出；
 *        跨过 long_ticks 调 on_long_start，其后每 repeat_ticks 调 on_long_hold
 * @param kb 控制结构指针
 * @return SKB_OK 成功，否则错误码
 */
skb_err_t skb_tick(skb_t *kb);

/**
 * @brief 查询某键当前按住时长（tick 数，具体时间单位由应用定义）
 * @param kb     控制结构指针
 * @param key_id 键号
 * @return 按住 tick 数；未按下或未找到返回 0
 */
uint32_t skb_hold_ticks(skb_t *kb, uint16_t key_id);

/**
 * @brief 查询某键当前是否处于按下状态（消抖确认后）
 * @param kb     控制结构指针
 * @param key_id 键号
 * @return true=按下；false=未按下或未找到
 */
bool skb_is_pressed(skb_t *kb, uint16_t key_id);

#endif /* _SIMPLE_KEYBOARD_H_ */
