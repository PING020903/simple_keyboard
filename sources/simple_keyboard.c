#include "simple_keyboard.h"
#include <string.h>

#ifdef SKB_DEBUG
#include "DBG_macro.h"
#endif

#if SKB_ARG_CHECK_ENABLE
#define SKB_ARG_CHECK(_kb)                                   \
    do                                                       \
    {                                                        \
        if (!(_kb))                                          \
            return SKB_ERR_ARG;                              \
        if (!(_kb)->initialized)                             \
            return SKB_ERR_NOT_READY;                        \
    } while (0)
#else
/* 校验关闭：调用者须保证 kb 已初始化且指针合法 */
#define SKB_ARG_CHECK(_kb) \
    do                     \
    {                      \
    } while (0)
#endif

/* 调用按键回调：handlers 与字段均为 NULL 检查 */
#define SKB_CALL_HANDLER(_key, _field, _info)                          \
    do                                                                 \
    {                                                                  \
        const skb_key_cfg_t *_cfg = (_key)->cfg;                       \
        if ((_cfg)->handlers != NULL && (_cfg)->handlers->_field != NULL) \
            (_cfg)->handlers->_field((_cfg)->key_id, (uint32_t)(_info), (_cfg)->param); \
    } while (0)

/* 按 key_id 查找按键运行时状态（线性查找，键数少时足够） */
static skb_key_t *_skb_find(skb_t *kb, uint16_t key_id)
{
    for (uint16_t i = 0; i < kb->key_count; i++)
    {
        if (kb->keys[i].cfg->key_id == key_id)
            return &kb->keys[i];
    }
    return NULL;
}

skb_err_t skb_init(skb_t *kb, const skb_key_cfg_t *cfg, uint16_t n,
                   uint16_t global_long_ticks, uint16_t global_repeat_ticks)
{
    if (kb == NULL || cfg == NULL)
        return SKB_ERR_ARG;
    if (n == 0U || n > SKB_MAX_KEYS)
        return SKB_ERR_TOO_MANY;

    memset(kb, 0, sizeof(skb_t));

    kb->key_count = n;
    kb->global_long_ticks = global_long_ticks;
    kb->global_repeat_ticks = global_repeat_ticks;

    for (uint16_t i = 0; i < n; i++)
    {
        skb_key_t *key = &kb->keys[i];
        if (cfg[i].read_raw == NULL)
            return SKB_ERR_ARG;

        key->cfg = &cfg[i]; /* 指向用户配置，不复制（省 RAM）*/
        /* 消抖初始电平取首次读取值（按未按下/按下作为起点）*/
        debounce_init(&key->db, cfg[i].read_raw(cfg[i].ctx) ? 1 : 0);
    }

    kb->initialized = true;

#ifdef SKB_DEBUG
    DEBUG_PRINT("skb_init: n=%d, long_ticks=%u, repeat_ticks=%u",
                (int)n, global_long_ticks, global_repeat_ticks);
#endif
    return SKB_OK;
}

skb_err_t skb_poll(skb_t *kb)
{
    SKB_ARG_CHECK(kb);

    for (uint16_t i = 0; i < kb->key_count; i++)
    {
        skb_key_t *key = &kb->keys[i];
        const skb_key_cfg_t *cfg = key->cfg;   /* 指向本键配置 */

        /* 读取原始电平（1=按下，0=松开）*/
        bool raw = cfg->read_raw(cfg->ctx);

        /* 消抖：debounce_sample 返回 1 表示电平刚发生翻转（边沿）*/
        bool edge = (debounce_sample(&key->db, raw) == 1);

        /* 消抖后的稳定电平（1=已确认按下，0=已确认松开）*/
        bool stable = (debounce_state(&key->db) == 1);

        if (edge)
        {
            if (stable)
            {
                /* 按下沿：清零计时，报告按下 */
                key->pressed = 1;
                key->press_ticks = 0;
#if SKB_LONG_REPEAT_ENABLED
                key->repeat_idx = 0;
#endif
#if SKB_CFG_ENABLE_LONG
                key->long_fired = 0;
#endif
#if SKB_CFG_ENABLE_PRESSED
                SKB_CALL_HANDLER(key, on_press, 0);
#endif
            }
            else
            {
                /* 松开沿：按是否已长按分类为 长按松开/短按 */
#if SKB_CFG_ENABLE_LONG
                if (key->long_fired)
                    SKB_CALL_HANDLER(key, on_long_up, key->press_ticks);
                else
#endif
                {
#if SKB_CFG_ENABLE_CLICK
                    SKB_CALL_HANDLER(key, on_click, key->press_ticks);
#endif
                }
                key->pressed = 0;
                key->press_ticks = 0;
#if SKB_LONG_REPEAT_ENABLED
                key->repeat_idx = 0;
#endif
#if SKB_CFG_ENABLE_LONG
                key->long_fired = 0;
#endif
            }
        }
    }

    return SKB_OK;
}

skb_err_t skb_tick(skb_t *kb)
{
    SKB_ARG_CHECK(kb);

    for (uint16_t i = 0; i < kb->key_count; i++)
    {
        skb_key_t *key = &kb->keys[i];

        if (!key->pressed)
            continue;

        /* 按住期间：press_ticks 累加 1，并评估长按/重复触发 */
        key->press_ticks++;

#if SKB_CFG_ENABLE_LONG
        {
            const uint16_t long_ticks = key->cfg->long_ticks ? key->cfg->long_ticks : kb->global_long_ticks;

            if (long_ticks != 0U && !key->long_fired && key->press_ticks >= long_ticks)
            {
                key->long_fired = 1;
#if SKB_LONG_REPEAT_ENABLED
                {
                    const uint16_t repeat_ticks = key->cfg->repeat_ticks ? key->cfg->repeat_ticks : kb->global_repeat_ticks;
                    key->next_repeat_ticks = long_ticks + (repeat_ticks != 0U ? (uint32_t)repeat_ticks : 0U);
                }
#endif
                SKB_CALL_HANDLER(key, on_long_start, key->press_ticks);
            }
        }
#endif

#if SKB_LONG_REPEAT_ENABLED
        if (key->long_fired && key->press_ticks >= key->next_repeat_ticks)
        {
            SKB_CALL_HANDLER(key, on_long_hold, key->repeat_idx);
            key->repeat_idx++;
            key->next_repeat_ticks += (uint32_t)(key->cfg->repeat_ticks ? key->cfg->repeat_ticks : kb->global_repeat_ticks);
        }
#endif
    }

    return SKB_OK;
}

uint32_t skb_hold_ticks(skb_t *kb, uint16_t key_id)
{
    skb_key_t *key;

    if (kb == NULL || !kb->initialized)
        return 0U;

    key = _skb_find(kb, key_id);
    if (key == NULL || !key->pressed)
        return 0U;

    return key->press_ticks;
}

bool skb_is_pressed(skb_t *kb, uint16_t key_id)
{
    skb_key_t *key;

    if (kb == NULL || !kb->initialized)
        return false;

    key = _skb_find(kb, key_id);
    if (key == NULL)
        return false;

    return (key->pressed == 1);
}
