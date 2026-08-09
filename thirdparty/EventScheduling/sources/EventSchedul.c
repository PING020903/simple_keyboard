#include "DBG_macro.h"
#include <string.h>
#include "EventSchedul.h"
#include "ringBuffer.h"

#define EVTSCHEDUL_DEBUG_CREATE_TASK 1

/* ==================== 调度器上下文结构体 ==================== */
struct EventSchedul_Context {
    EventSchedul_Allocator alloc;                        /* 注入的内存分配器 */
#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC)
    EventSchedul_TaskNode taskPool[EVTSCHEDUL_TASKS_MAX]; /* 静态任务节点池 */
#endif
#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    ll_t taskList;                                       /* 活动任务链表头 */
#endif
    void (*sleepMethod)(void);
    EventSchedul_TaskId   taskNum;
    uint8_t  ringBuf_buffer[EVTSCHEDUL_TASKS_QUEUE_MAX * sizeof(EventSchedul_TaskQueue)];
    ringbuf_t ringTaskQueue;
};

/* ==================== 生命周期 ==================== */

EventSchedul_Context* EventSchedul_Create(const EventSchedul_Allocator* allocator)
{
    EventSchedul_Context* ctx;

    if (!allocator || !allocator->malloc || !allocator->free)
        return NULL;

    ctx = (EventSchedul_Context*)allocator->malloc(sizeof(EventSchedul_Context));
    if (!ctx)
        return NULL;

    ctx->alloc       = *allocator;
    ctx->sleepMethod = NULL;
    ctx->taskNum     = EVTSCHEDUL_INIT_TASK_ID;

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 初始化活动任务链表（空链表：head 指向自身） */
    ctx->taskList = (ll_t)ll_head_INIT(ctx->taskList);
#endif

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC)
    /* 静态池：直接清零，后续通过数组下标扫描空闲槽位 */
    memset(ctx->taskPool, 0, sizeof(ctx->taskPool));
#endif

    /* 初始化环形缓冲区（const 成员无法直接赋值，用 memcpy 拷贝） */
    {
        ringbuf_t tmp = RINGBUFCRTL_INIT(ctx->ringBuf_buffer,
                                          EVTSCHEDUL_TASKS_QUEUE_MAX,
                                          sizeof(EventSchedul_TaskQueue), false);
        memcpy(&ctx->ringTaskQueue, &tmp, sizeof(ringbuf_t));
    }
    return ctx;
}

void EventSchedul_Destroy(EventSchedul_Context* ctx)
{
    EventSchedul_free_fn freeFn;

    if (!ctx)
        return;

    freeFn = ctx->alloc.free; /* 先缓存，ctx 自身释放后不可再访问 */

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 释放所有动态分配的任务节点 */
    {
        EventSchedul_TaskNode *task, *tmp;
        list_for_each_entry_safe(task, tmp, &ctx->taskList, node)
        {
            list_del(&task->node);
            freeFn(task);
        }
    }
#endif

    /* 释放上下文自身 */
    freeFn(ctx);
}

/* ==================== 内部辅助函数 ==================== */

/* 将任务事件推入队列 */
static inline int pushTaskToQueue(EventSchedul_Context* ctx, const EventSchedul_TaskQueue* task)
{
    ringBuf_err_t err;
    if (!task)
        return EVTSCHEDUL_ERR_ARG;

    err = ringBuf_push(&ctx->ringTaskQueue, task);
    if (err == RINGBUF_ERR_WR_DENIED)
        return EVTSCHEDUL_ERR_MEM;
    if (err != RINGBUF_OK)
        return EVTSCHEDUL_ERR_FAIL;

    return EVTSCHEDUL_OK;
}

/* 从队列中取出任务事件 */
static inline int pullTaskWithQueue(EventSchedul_Context* ctx, EventSchedul_TaskNode** task)
{
    EventSchedul_TaskQueue queueItem;
    ringBuf_err_t err;

    if (!task && (*task) != NULL)
        return EVTSCHEDUL_ERR_ARG;

    err = ringBuf_pop(&ctx->ringTaskQueue, &queueItem);
    if (err == RINGBUF_ERR_EMPTY)
        return EVTSCHEDUL_ERR_NOTHING;
    if (err != RINGBUF_OK)
        return EVTSCHEDUL_ERR_FAIL;

    /* 取出任务句柄&触发事件值 */
    *task = queueItem.taskHandle;
    (*task)->info.eventTrigger = queueItem.eventTrigger;

    return EVTSCHEDUL_OK;
}

/* 寻找或创建空闲节点 */
static EventSchedul_TaskNode* FindOrCreateFreeTaskNode(EventSchedul_Context* ctx)
{
    EventSchedul_TaskNode* task = NULL;

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC)
    /* 静态模式：扫描数组找空闲槽位（pTaskFunc == NULL 即空闲） */
    for (int i = 0; i < EVTSCHEDUL_TASKS_MAX; i++) {
        if (ctx->taskPool[i].pTaskFunc == NULL) {
            return &ctx->taskPool[i];
        }
    }
    MACRO_PRINT_INT(EVTSCHEDUL_ERR_MEM);
    return NULL;
#endif

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 动态模式：分配新节点 */
    task = (EventSchedul_TaskNode*)ctx->alloc.malloc(sizeof(EventSchedul_TaskNode));
    if (task == NULL)
        return NULL;

    /* 为新节点赋初值 */
    memset(task, 0, sizeof(EventSchedul_TaskNode));
    return task;
#endif

    /* 给定固定返回值 */
    MACRO_PRINT_INT(EVTSCHEDUL_ERR_FAIL);
    return NULL;
}

/* ==================== 公开 API ==================== */

/* 注册任务 — 成功返回有效句柄地址, 失败返回NULL */
EventSchedul_TaskNode* EventSchedul_TaskRegister(EventSchedul_Context* ctx, const EventSchedul_TaskNode* cfg)
{
    EventSchedul_TaskNode* task = NULL;
    if (!ctx || !cfg || !cfg->pTaskFunc)
        return NULL;

    task = FindOrCreateFreeTaskNode(ctx);
#if !EVTSCHEDUL_DEBUG_CREATE_TASK
    VAR_PRINT_POS(task);
#endif
    if (!task)
        return NULL;

    task->pTaskFunc       = cfg->pTaskFunc;
    task->pTaskFuncArg    = cfg->pTaskFuncArg;
    task->info.eventStart = cfg->info.eventStart;
    task->info.eventEnd   = cfg->info.eventEnd;
    task->info.taskId     = ++ctx->taskNum;

    /* 限制任务ID为正整数 */
    if (ctx->taskNum < (EventSchedul_TaskId)0) {
#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
        ctx->alloc.free(task);
#else
        /* 静态模式：清零归还 */
        memset(task, 0, sizeof(EventSchedul_TaskNode));
#endif
        return NULL;
    }

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 加入活动任务链表 */
    list_add_tail(&task->node, &ctx->taskList);
#endif

    VAR_PRINT_UD(task->info.taskId);
    return task;
}

int EventSchedul_TaskUnRegister(EventSchedul_Context* ctx, EventSchedul_TaskNode* taskHandle)
{
    if (!ctx || !taskHandle)
        return EVTSCHEDUL_ERR_ARG;

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 从活动链表移除 */
    list_del(&taskHandle->node);
#endif

    DEBUG_PRINT("1[%x] 2[%x] 3[%x] 4[%u] 5[%u]",
        taskHandle->info.eventStart,
        taskHandle->info.eventEnd,
        taskHandle->info.eventTrigger,
        taskHandle->info.taskId,
        taskHandle->info.executeCount);

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC)
    /* 静态模式：清除任务信息，pTaskFunc=NULL 即标记为空闲 */
    memset(taskHandle, 0, sizeof(EventSchedul_TaskNode));
#endif

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_DYNAMIC)
    /* 动态模式：直接释放 */
    memset(taskHandle, 0, sizeof(EventSchedul_TaskNode));
    ctx->alloc.free(taskHandle);
#endif

    return EVTSCHEDUL_OK;
}

/* 给任务发送事件 */
int EventSchedul_setEventToTask(EventSchedul_Context* ctx, const EventSchedul_TaskNode* target,
                                EventSchedul_EventId TaskEvent)
{
    int ret = EVTSCHEDUL_OK;
    const EventSchedul_TaskQueue tmp = {
        .taskHandle   = (EventSchedul_TaskNode*)target,
        .eventTrigger = TaskEvent
    };
    if (!ctx || !target)
        return EVTSCHEDUL_ERR_ARG;

#if (EVTSCHEDUL_TASKS_MODE == EVTSCHEDUL_STATIC)
    /* 静态模式：检查 target 指针是否在 taskPool 范围内且已注册 */
    if (target < &ctx->taskPool[0] || target >= &ctx->taskPool[EVTSCHEDUL_TASKS_MAX]
        || target->pTaskFunc == NULL)
        return EVTSCHEDUL_ERR_TASK;
#else
    /* 动态模式：遍历链表查找目标任务 */
    {
        EventSchedul_TaskNode* current = NULL;
        int found = 0;
        list_for_each_entry(current, &ctx->taskList, node)
        {
            if (current == target) {
                found = 1;
                break;
            }
        }
        if (!found)
            return EVTSCHEDUL_ERR_TASK;
    }
#endif

    switch (TaskEvent) {
    case EVTSCHEDUL_INVALID_EVT:
    case EVTSCHEDUL_INIT_EVT:
        return EVTSCHEDUL_ERR_ARG;
    default:
        /* 判断设置的事件值是否在注册的事件值区间 */
        ret = (target->info.eventStart > target->info.eventEnd) ? 1 : 0;
        if (ret) {
            /* 中间区间没有注册 */
            if (TaskEvent < target->info.eventStart && TaskEvent >= target->info.eventEnd)
                return EVTSCHEDUL_ERR_EVENT;
        } else {
            /* 左右区间没有注册 */
            if (TaskEvent < target->info.eventStart || TaskEvent >= target->info.eventEnd)
                return EVTSCHEDUL_ERR_EVENT;
        }

        break;
    }

    ret = pushTaskToQueue(ctx, &tmp);
    DEBUG_PRINT("pushTaskToQueue:[%s]", (ret == EVTSCHEDUL_OK) ? "OK" : "FAIL");
    return ret;
}

#if EVTSCHEDUL_TEST
static void test1(EventSchedul_EventId taskEvt, void* arg) {
    DEBUG_PRINT("");
}
static void test2(EventSchedul_EventId taskEvt, void* arg) {
    DEBUG_PRINT("");
}
static void test3(EventSchedul_EventId taskEvt, void* arg) {
    DEBUG_PRINT("");
}

void evtSchedul_test(EventSchedul_Context* ctx, const EventSchedul_TaskNode* task)
{
    int ret = EVTSCHEDUL_OK;
    EventSchedul_TaskQueue testQueue = { .taskHandle = (EventSchedul_TaskNode*)task };
    EventSchedul_TaskNode eCfg = {
        .pTaskFuncArg    = NULL,
        .info.eventStart = 0x0001,
        .info.eventEnd   = 0x000f
    };
    EventSchedul_TaskNode* current = NULL;
    if (!ctx || !task)
        return;
#if 0
    testQueue.eventTrigger = 0x0001;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0x0000;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0xffff;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0x000f;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0x011f;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0xfff1;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);

    testQueue.eventTrigger = 0xfff2;
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);
    DEBUG_PRINT("evt[%x], ret[%d]", testQueue.eventTrigger, ret);


    while (pullTaskWithQueue(ctx, &current) == EVTSCHEDUL_OK) {
        DEBUG_PRINT("--evt[%x]", current->info.eventTrigger);
    }
#endif /* 简单测试环形FIFO队列, pass (2025.12.04) */
#if 0
    testQueue.eventTrigger = 0x0001;
    current = list_first_entry(&ctx->taskList, EventSchedul_TaskNode, node);
    VAR_PRINT_POS(current);
    ret = EventSchedul_setEventToTask(ctx, testQueue.taskHandle, testQueue.eventTrigger);

    EventSchedul_MainLoop(ctx);
#endif /* 简单测试事件调度循环(静态条件), pass (2025.12.05) */
      /* 简单测试事件调度循环(动态条件), pass (2025.12.05) */
#if 0
    eCfg.pTaskFunc = test1;
    current = EventSchedul_TaskRegister(ctx, &eCfg);
    VAR_PRINT_POS(current);

    eCfg.pTaskFunc = test2;
    testQueue.taskHandle = EventSchedul_TaskRegister(ctx, &eCfg);
    VAR_PRINT_POS(current);

    eCfg.pTaskFunc = test3;
    current = EventSchedul_TaskRegister(ctx, &eCfg);
    VAR_PRINT_POS(current);

    {
        EventSchedul_TaskNode* iter;
        list_for_each_entry(iter, &ctx->taskList, node)
        {
            DEBUG_PRINT("current[%p], node.next[%p], node.prev[%p], taskId[%d]",
                iter, iter->node.next, iter->node.prev, iter->info.taskId);
        }
    }

    DEBUG_PRINT("unreg ret[%d]", EventSchedul_TaskUnRegister(ctx, testQueue.taskHandle));
    {
        EventSchedul_TaskNode* iter;
        list_for_each_entry(iter, &ctx->taskList, node)
        {
            DEBUG_PRINT("current[%p], node.next[%p], node.prev[%p], taskId[%d]",
                iter, iter->node.next, iter->node.prev, iter->info.taskId);
        }
    }
#endif /* 动态链表增加删除测试, pass (2025.12.05) */
}
#endif /* EVTSCHEDUL_TEST */

int EventSchedul_RegSleepMethod(EventSchedul_Context* ctx, void* pFunc)
{
    if (!ctx || !pFunc)
        return EVTSCHEDUL_ERR_ARG;

    ctx->sleepMethod = (void (*)(void))pFunc;
    return EVTSCHEDUL_OK;
}

int EventSchedul_MainLoop(EventSchedul_Context* ctx)
{
    int ret = EVTSCHEDUL_OK;
    EventSchedul_TaskNode* current = NULL;

    if (!ctx)
        return EVTSCHEDUL_ERR_ARG;

    /* 初始化环形缓冲区 */
    ringBuf_init(&ctx->ringTaskQueue);

    if (!ctx->sleepMethod)
        goto _end;
    while (1) {
        current = NULL;
        ret = pullTaskWithQueue(ctx, &current);
        DEBUG_PRINT("pullTaskWithQueue:[%s]", (ret == EVTSCHEDUL_OK) ? "OK" : "FAIL");
        ctx->sleepMethod();
        if (ret != EVTSCHEDUL_OK || current == NULL)
            continue;

        current->pTaskFunc(current->info.eventTrigger, current->pTaskFuncArg);
        ctx->sleepMethod();
    }

_end:
    return EVTSCHEDUL_ERR_FAIL;
}

int EventSchedul_TmosPoll(EventSchedul_Context* ctx)
{
    int ret;
    EventSchedul_TaskNode* current = NULL;

    if (!ctx)
        return EVTSCHEDUL_ERR_ARG;

    /* 尝试取一个事件（非阻塞） */
    ret = pullTaskWithQueue(ctx, &current);
    if (ret != EVTSCHEDUL_OK || current == NULL)
        return ret;

    /* 执行任务回调 */
    current->pTaskFunc(current->info.eventTrigger, current->pTaskFuncArg);
    return EVTSCHEDUL_OK;
}
