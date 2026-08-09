#ifndef _EVENTSCHEDUL_H_
#define _EVENTSCHEDUL_H_

#include <stddef.h>
#include "ll.h"

/* ==================== 调度器上下文（不透明类型，支持多实例） ==================== */
typedef struct EventSchedul_Context EventSchedul_Context;

#define EVTSCHEDUL_DYNAMIC 0
#define EVTSCHEDUL_STATIC 1
#define EVTSCHEDUL_TASKS_MODE EVTSCHEDUL_STATIC

#define EVTSCHEDUL_TEST 0

typedef enum {
    EVTSCHEDUL_OK         = 0,  /* 成功 */
    EVTSCHEDUL_ERR_FAIL,        /* 通用失败 */
    EVTSCHEDUL_ERR_ARG,         /* 参数错误 */
    EVTSCHEDUL_ERR_MEM,         /* 内存不足 */
    EVTSCHEDUL_ERR_NOTHING,     /* 无数据 */
    EVTSCHEDUL_ERR_EVENT,       /* 事件错误 */
    EVTSCHEDUL_ERR_TASK,        /* 任务错误 */
} EventSchedul_ErrCode;

/* ==================== 领域类型定义（替代裸 unsigned short / short） ==================== */

typedef unsigned short EventSchedul_EventId;   /* 事件编号 */
typedef short          EventSchedul_TaskId;    /* 任务编号 */
typedef unsigned short EventSchedul_ExecCount; /* 执行次数 */

#define EVTSCHEDUL_INVALID_EVT    ((EventSchedul_EventId)(0x00u-0x01u))
#define EVTSCHEDUL_INIT_EVT       ((EventSchedul_EventId)0x0000U)
#define EVTSCHEDUL_INIT_TASK_ID   ((EventSchedul_TaskId)0)
#define EVTSCHEDUL_INVALID_TASK_ID ((EventSchedul_TaskId) - 1)

#define EVTSCHEDUL_TASKS_MAX 8
#if EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC
#define EVTSCHEDUL_TASKS_QUEUE_MAX (EVTSCHEDUL_TASKS_MAX * 2)
#else
#define EVTSCHEDUL_TASKS_QUEUE_MAX 32
#endif

/*
* 若短时间内有不同的事件被触发,
* 可能会导致最后触发的事件把先前的事件都覆盖
* 
*/



typedef struct {
    EventSchedul_EventId  eventStart;   /* 事件起始号 */
    EventSchedul_EventId  eventEnd;     /* 事件结束号 */
    EventSchedul_EventId  eventTrigger; /* 触发事件号 */
    EventSchedul_TaskId   taskId;       /* 任务编号 */
    EventSchedul_ExecCount executeCount;/* 任务运行次数 */
} EventSchedul_TaskNodeInfo;

typedef void (*EventSchedul_pTaskFunc)(EventSchedul_EventId RecvEvt, void* arg);
typedef struct EventSchedul_TaskNode {
    ll_t node;                   /* c-linked-list 节点（替代手写 next/prev） */
    EventSchedul_TaskNodeInfo info;
    EventSchedul_pTaskFunc pTaskFunc;
    void* pTaskFuncArg;
} EventSchedul_TaskNode;

typedef struct {
    EventSchedul_TaskNode* taskHandle;
    EventSchedul_EventId   eventTrigger;
} EventSchedul_TaskQueue;

/* ==================== 可注入的内存管理接口（解除 stdlib.h 依赖） ==================== */

/**
 * @brief 内存分配函数指针类型（与标准库 malloc 签名兼容）
 * @param size 分配字节数
 * @return 成功返回内存指针，失败返回 NULL
 */
typedef void *(*EventSchedul_malloc_fn)(size_t size);

/**
 * @brief 内存释放函数指针类型（与标准库 free 签名兼容）
 * @param ptr 要释放的内存指针
 */
typedef void (*EventSchedul_free_fn)(void *ptr);

/**
 * @brief 内存分配器结构体（注入以替代 stdlib，便于后续扩展）
 */
typedef struct {
    EventSchedul_malloc_fn malloc;
    EventSchedul_free_fn   free;
} EventSchedul_Allocator;

/* ==================== 调度器生命周期 ==================== */

/**
 * @brief 创建调度器实例
 * @param allocator 内存分配器（注入 malloc/free 以解除 stdlib 依赖）
 * @return 成功返回上下文句柄，失败返回 NULL
 */
EventSchedul_Context* EventSchedul_Create(const EventSchedul_Allocator* allocator);

/**
 * @brief 销毁调度器实例（自动释放所有任务节点及上下文自身）
 * @param ctx 调度器上下文句柄
 */
void EventSchedul_Destroy(EventSchedul_Context* ctx);

/* ==================== 任务管理 ==================== */

/**
 * @brief 注册任务到调度器
 *
 * 将任务节点注册到调度器的活动任务列表中。静态模式下从 taskPool 分配槽位，
 * 动态模式下通过注入的 malloc 分配节点内存。任务 ID 由调度器自动递增分配。
 *
 * @param ctx 调度器上下文句柄
 * @param cfg 任务配置（pTaskFunc 必填，eventStart/eventEnd 定义可接收的事件区间）
 * @return 成功返回任务句柄（即任务节点指针），失败返回 NULL
 *
 * @note 静态模式最多注册 EVTSCHEDUL_TASKS_MAX 个任务，超限返回 NULL
 */
EventSchedul_TaskNode* EventSchedul_TaskRegister(EventSchedul_Context* ctx, const EventSchedul_TaskNode* cfg);

/**
 * @brief 注销任务
 *
 * 将任务从调度器的活动列表中移除。静态模式下仅清零节点标记为空闲槽位；
 * 动态模式下会调用注入的 free 释放节点内存。
 *
 * @param ctx     调度器上下文句柄
 * @param taskHandle 待注销的任务句柄（由 TaskRegister 返回）
 * @return EVTSCHEDUL_OK       — 成功
 * @return EVTSCHEDUL_ERR_ARG  — ctx 或 taskHandle 为 NULL
 */
int EventSchedul_TaskUnRegister(EventSchedul_Context* ctx, EventSchedul_TaskNode* taskHandle);

/**
 * @brief 向指定任务投递事件
 *
 * 将事件推入环形队列 FIFO 等待消费。会校验目标任务是否仍在活动列表中，
 * 以及事件号是否在任务注册时声明的事件区间内。
 *
 * @param ctx      调度器上下文句柄
 * @param task     目标任务句柄（由 TaskRegister 返回）
 * @param TaskEvent 事件号（必须在 [eventStart, eventEnd) 区间内）
 * @return EVTSCHEDUL_OK        — 事件已入队
 * @return EVTSCHEDUL_ERR_ARG   — ctx/task 为 NULL，或为 INVALID_EVT / INIT_EVT
 * @return EVTSCHEDUL_ERR_TASK  — 目标任务不在活动列表中（已注销）
 * @return EVTSCHEDUL_ERR_EVENT — 事件号不在任务注册的区间内
 * @return EVTSCHEDUL_ERR_MEM   — 环形队列满
 */
int EventSchedul_setEventToTask(EventSchedul_Context* ctx, const EventSchedul_TaskNode* task,
    EventSchedul_EventId TaskEvent);

#if EVTSCHEDUL_TEST
void evtSchedul_test(EventSchedul_Context* ctx, const EventSchedul_TaskNode* task);
#endif

/**
 * @brief 注册休眠回调（供 MainLoop 使用）
 *
 * MainLoop 在每次轮询间隙调用此函数以让出 CPU。
 *
 * @param ctx   调度器上下文句柄
 * @param pFunc 休眠函数指针（void (*)(void) 签名）
 * @return EVTSCHEDUL_OK      — 成功
 * @return EVTSCHEDUL_ERR_ARG — ctx 或 pFunc 为 NULL
 */
int EventSchedul_RegSleepMethod(EventSchedul_Context* ctx, void* pFunc);

/**
 * @brief 阻塞式事件调度主循环
 *
 * 内部 while(1) 死循环，不断从环形队列中取出事件并执行对应任务回调。
 * 每次轮询前后各调用一次 sleepMethod 做节流。需要在注册 sleepMethod 后使用。
 * 如需非阻塞协作式调度，请使用 EventSchedul_TmosPoll。
 *
 * @param ctx 调度器上下文句柄
 * @return 仅在 sleepMethod 未设置时返回 EVTSCHEDUL_ERR_FAIL
 */
int EventSchedul_MainLoop(EventSchedul_Context* ctx);

/**
 * @brief TMOS 协作式轮询函数（无中断，非阻塞）
 *
 * 每次调用只从事件队列取出一个事件并执行对应任务回调，
 * 然后立即返回。适用于无抢占、无中断保存上下文的 TMOS 协作调度环境。
 *
 * @param ctx 调度器上下文句柄
 * @return EVTSCHEDUL_OK          — 成功处理一个事件
 * @return EVTSCHEDUL_ERR_NOTHING — 队列为空，无事可做
 * @return 其他错误码             — 参数/内存等错误
 */
int EventSchedul_TmosPoll(EventSchedul_Context* ctx);

#endif // !_EVENTSCHEDUL_H_