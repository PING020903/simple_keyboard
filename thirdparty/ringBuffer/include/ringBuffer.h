#pragma once
#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <stdint.h>
#include <stdbool.h>

/* 组件内部调试打印为可选项：仅在构建时定义 RING_DEBUG（如 -DRING_DEBUG）才启用。
 * 默认关闭，避免把 snprintf 及其栈/代码开销带入发布构建。 */

/* 参数校验开关（含指针 NULL 检查与缓冲区配置校验）：
 * 默认 1，拦截 NULL 指针及被篡改的 buffer/depth/item_size 等非法配置；
 * 置 0（-DRINGBUF_ARG_CHECK_ENABLE=0）时全部校验编译为空，
 * 视为调用者完全可信，以换取最小代码与运行时开销。 */
#ifndef RINGBUF_ARG_CHECK_ENABLE
#define RINGBUF_ARG_CHECK_ENABLE 1
#endif

#ifndef MIN
#define MIN(n, m) (((n) < (m)) ? (n) : (m))
#endif

#ifndef MAX
#define MAX(n, m) (((n) < (m)) ? (m) : (n))
#endif

typedef enum
{
    RINGBUF_OK = 0,
    RINGBUF_ERR_FAIL,
    RINGBUF_ERR_ARG,
    RINGBUF_ERR_BUF,
    RINGBUF_ERR_WR_DENIED,
    RINGBUF_ERR_INVALID_PTR,
    RINGBUF_ERR_EMPTY,
    RINGBUF_ERR_IDX,
} ringBuf_err_t;

typedef void *ringbuf_mutex_t;               // 互斥锁句柄
typedef void (*ringbuf_lock_func_t)(void);   // 加锁函数指针
typedef void (*ringbuf_unlock_func_t)(void); // 解锁函数指针

typedef unsigned int ringbuf_uidx_t;   // 无符号索引类型
typedef unsigned short ringbuf_ucnt_t; // 无符号计数类型
typedef int ringbuf_idx_t;             // 索引类型
typedef short ringbuf_cnt_t;           // 计数类型

typedef struct ringbuf_t
{
    void *buffer;                   /**< 数据缓冲区指针 */
    const ringbuf_ucnt_t depth;     /**< 缓冲区深度（元素个数） 运行时不可改变*/
    const ringbuf_ucnt_t item_size; /**< 单个元素大小（字节） 运行时不可改变*/

    volatile ringbuf_uidx_t wr_idx; /**< 写索引（未掩码，范围 0~2*depth-1） */
    volatile ringbuf_uidx_t rd_idx; /**< 读索引（未掩码，范围 0~2*depth-1） */

    bool overwritable; /**< 满时是否覆盖旧数据 */
} ringbuf_t;

typedef ptrdiff_t ringBuf_ptr_t;

#define RINGBUFCRTL_INIT(_buffer, _depth, _item_sz, _overwrite) \
    {                                                           \
        .buffer = (void *)(_buffer),                            \
        .depth = (_depth),                                      \
        .item_size = (_item_sz),                                \
        .wr_idx = 0,                                            \
        .rd_idx = 0,                                            \
        .overwritable = ((_overwrite) ? true : false),          \
    }

ringBuf_err_t ringBuf_clear(ringbuf_t *rb);

ringBuf_err_t ringBuf_count(const ringbuf_t *rb, ringbuf_cnt_t *pCount);

ringBuf_err_t ringBuf_init(ringbuf_t *rb);

ringBuf_err_t ringBuf_push(ringbuf_t *rb, const void *pData);

ringBuf_err_t ringBuf_pop(ringbuf_t *rb, void *pData);

ringBuf_err_t ringBuf_peek(const ringbuf_t *rb, void *pData, const ringbuf_ucnt_t itemIdx);

ringBuf_err_t ringBuf_push_multi(ringbuf_t *rb, const void *pData, const ringbuf_ucnt_t dataCount, ringbuf_cnt_t *pCount);

ringBuf_err_t ringBuf_pop_multi(ringbuf_t *rb, void *pData, const ringbuf_ucnt_t dataCount, ringbuf_cnt_t *pCount);

ringBuf_err_t ringBuf_peek_multi(const ringbuf_t *rb, void *pData, const ringbuf_ucnt_t dataCount,
                                 const ringbuf_cnt_t itemIdx, ringbuf_cnt_t *pCount);

#endif