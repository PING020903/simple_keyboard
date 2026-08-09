/**
 * @file memGroundP.c
 * @brief 内存池管理模块实现文件
 * @details 实现基于预分配内存的内存池管理功能
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "memGroundP.h"
#if MGP_DEBUG
#include "DBG_macro.h"

#define MGP_CREATE_POOL_DEBUG 0
#define MGP_CTRL_BLOCK_DEBUG 0
#define MGP_FREE_BLOCK_DEBUG 0
#define MGP_MEMORY_FREE_DEBUG 0
#define MGP_MEMORY_ALLOC_DEBUG 0
#else
#endif

#define mgp_cast(t, exp) ((t)(exp))
#define mgp_min(a, b) ((a) < (b) ? (a) : (b))
#define mgp_max(a, b) ((a) > (b) ? (a) : (b))

/** @defgroup MGP_INTERNAL 内部宏定义
 *  @{
 */
#define HGIH_TO_LOW 0                     ///< 从高到低排序模式
#define LOW_TO_HIGH 1                     ///< 从低到高排序模式
#define CTRL_BLOCK_QSORT_MDOE HGIH_TO_LOW ///< 控制块快速排序模式（当前设置为从高到低）
/**  @} */

/**
 * @struct mgp_ctrl_t
 * @brief 内存块控制结构
 * @details 用于管理每个分配的内存块，包含缓冲区大小和头尾保护标记位置
 *
 * 内存布局：[头部标记][用户缓冲区][尾部标记]
 * - pHead: 指向头部标记位置
 * - pTail: 指向尾部标记位置
 * - pBuff: 指向用户实际可用的缓冲区起始位置（头部标记之后）
 * - buffSize: 包含头尾标记的总大小
 */
typedef struct
{
    size_t buffSize; ///< 缓冲区总大小（包含头尾标记）
#if MGP_MG_NUM
    unsigned int *pHead; ///< 指向头部保护标记位置
    unsigned int *pTail; ///< 指向尾部保护标记位置
#endif
    void *pBuff; ///< 指向用户可用缓冲区起始位置
    // 多申请 sizeof(unsigned int) 字节用以作为 MGP_tail
} mgp_ctrl_t;

/**
 * @struct mgp_pool_t
 * @brief 内存池控制结构
 * @details 管理整个内存池的状态和信息
 *
 * 内存池布局：[mgp_pool_t][mgp_ctrl_t 数组][已分配的内存块]
 */
typedef struct
{
    mgp_t baseAddr;        ///< 内存池基地址
    mgp_ctrl_t *ctrlTable; ///< 控制块表指针
    size_t totalSize;      ///< 内存池总大小（字节）
    size_t usedSize;       ///< 已使用的大小（字节）
    size_t freeSize;       ///< 连续空闲的大小 （字节）
    int allocBlockCnt;     ///< 已分配的内存块数量
    int bitFlag;           ///< 位标志
} mgp_pool_t;

// 一个有符号整数类型。它的主要作用是用来存储两个指针相减的结果，表示它们之间元素的个数或内存地址的差值。
typedef ptrdiff_t mgp_ptr_t;

// 向上对齐
static size_t align_up(size_t x, size_t align)
{
    assert(0 == (align & (align - 1)) && "must align to a power of two");
    return (x + (align - 1)) & ~(align - 1);
}

/**
 * @brief 向下对齐
 * @param[in] x 需要对齐的数值
 * @param[in] align 对齐值（必须为 2 的幂）
 * @return 对齐后的值
 * @details 将数值 x 向下对齐到 align 的整数倍
 */
static size_t align_down(size_t x, size_t align)
{
    assert(0 == (align & (align - 1)) && "must align to a power of two");
    return x - (x & (align - 1));
}

/**
 * @brief 指针对齐
 * @param[in] ptr 需要对齐的指针
 * @param[in] align 对齐值（必须为 2 的幂）
 * @return 对齐后的指针
 * @details 将指针向上对齐到指定边界
 */
static void *align_ptr(const void *ptr, size_t align)
{
    const mgp_ptr_t aligned =
        (mgp_cast(mgp_ptr_t, ptr) + (align - 1)) & ~(align - 1);
    assert(0 == (align & (align - 1)) && "must align to a power of two");
    return mgp_cast(void *, aligned);
}

/**
 * @brief 使用给定的内存区域创建内存池
 * @param[in] mem 指向预分配的内存区域起始地址
 * @param[in] bytes 内存区域的总大小（字节）
 * @return 成功返回内存池句柄，失败返回 NULL
 */
mgp_t mgp_create_with_pool(void *mem, size_t bytes)
{
    assert(mem);
    const size_t memMinSize = sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t); // 池记录表 + 块记录区
    if (bytes < memMinSize)
        return NULL;

    const size_t poolTabSize = sizeof(mgp_pool_t);
    const mgp_pool_t info = {
        .baseAddr = mem,
        .totalSize = bytes,
        .allocBlockCnt = 0U,
        .usedSize = poolTabSize,
        .freeSize = bytes - poolTabSize,
        .ctrlTable = mgp_cast(mgp_ctrl_t *, (mgp_ptr_t)mem + poolTabSize),
        .bitFlag = 0U,
    };

    memset(mem, 0X00, bytes);
    memcpy(mem, &info, sizeof(info));
#if MGP_CREATE_POOL_DEBUG
    const mgp_ptr_t memHead = (mgp_ptr_t)mem;
    const mgp_ptr_t memTail = (mgp_ptr_t)mem + (mgp_ptr_t)bytes;
    const mgp_pool_t *pInfo = mem;
    VAR_PRINT_HEX(memHead);
    VAR_PRINT_HEX(memTail);
    VAR_PRINT_UD(pInfo->usedSize);
    VAR_PRINT_UD(pInfo->totalSize);
    VAR_PRINT_UD(pInfo->allocBlockCnt);
    VAR_PRINT_HEX((mgp_ptr_t)pInfo->ctrlTable);
    VAR_PRINT_HEX((mgp_ptr_t)pInfo->baseAddr);
    VAR_PRINT_HEX((mgp_ptr_t)mem);
#endif
    return info.baseAddr;
}

/**
 * @brief 控制块比较函数（用于快速排序、快速查找）
 * @param[in] a 第一个控制块指针
 * @param[in] b 第二个控制块指针
 * @return 按照 pHead 地址大小比较结果
 * @details
 * 根据 CTRL_BLOCK_QSORT_MDOE 配置决定排序方向：
 * - HGIH_TO_LOW: 按地址从高到低排序
 * - LOW_TO_HIGH: 按地址从低到高排序
 * @note 该函数作为 qsort 的比较回调函数
 */
/**
 * @brief 控制块比较函数（用于快速排序、快速查找）
 * @param[in] a 第一个控制块指针
 * @param[in] b 第二个控制块指针
 * @return 按照 pBuff 地址大小比较结果
 * @details
 * 根据 CTRL_BLOCK_QSORT_MDOE 配置决定排序方向：
 * - HGIH_TO_LOW: 按地址从高到低排序
 * - LOW_TO_HIGH: 按地址从低到高排序
 * @note 该函数作为 qsort 的比较回调函数
 */
static int mgp_ctrlBlockCompare(const void *a, const void *b)
{
    assert(a);
    assert(b);
    const mgp_ptr_t posA = (mgp_ptr_t)((mgp_ctrl_t *)a)->pBuff;
    const mgp_ptr_t posB = (mgp_ptr_t)((mgp_ctrl_t *)b)->pBuff;
#if 0
    Sleep(1000);
    VAR_PRINT_HEX(posA);
    VAR_PRINT_HEX(posB);
    DEBUG_PRINT("");
#endif
#if 1
    if (posA == posB)
        return 0;
#endif
#if (CTRL_BLOCK_QSORT_MDOE == LOW_TO_HIGH)
    return (ctrlA->pBuff > ctrlB->pBuff) ? 1 : -1;
#elif (CTRL_BLOCK_QSORT_MDOE == HGIH_TO_LOW)
    return (posA < posB) ? 1 : -1;
#endif
}

/**
 * @brief 计算内存池可分配的最大内存块大小
 * @param[in] p 内存池句柄
 * @return 可分配的最大字节数
 */
const size_t mgp_canAllocMaxSize(mgp_t p)
{
    assert(p);
    const mgp_pool_t *pool = p;
    if(pool->allocBlockCnt == 0){
        const size_t overhead = sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t) +
                                ((MGP_MG_NUM) ? (sizeof(unsigned int) * 2) : 0);
        return pool->totalSize - overhead;
    }
    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompare);
#if MGP_MG_NUM
    const mgp_ptr_t endAddr = mgp_cast(mgp_ptr_t, pool->ctrlTable[pool->allocBlockCnt - 1].pHead);
#else
    const mgp_ptr_t endAddr = mgp_cast(mgp_ptr_t, pool->ctrlTable[pool->allocBlockCnt - 1].pBuff);
    const size_t ctrlOverhead = sizeof(mgp_ctrl_t);
    const mgp_ptr_t startAddr = ((mgp_ptr_t)&pool->ctrlTable[pool->allocBlockCnt]) + ctrlOverhead;
    return endAddr - startAddr;
#endif
}

/**
 * @brief 内存碰撞检查
 * @param[in] newCtrl 新的控制块指针
 * @param[in] pBuff 缓冲区指针
 * @return 发生碰撞返回非 0，无碰撞返回 0
 * @details 检查控制块是否与缓冲区地址重叠
 */
static int mgp_MemoryCollisionCheck(const mgp_ctrl_t *newCtrl, const void *pBuff)
{
    assert(newCtrl);
    assert(pBuff);

    mgp_ptr_t ctrlEndAddr = ((mgp_ptr_t)newCtrl) + sizeof(*newCtrl);
    mgp_ptr_t pBuffAddr = (mgp_ptr_t)pBuff;
#if MGP_CTRL_BLOCK_DEBUG
    VAR_PRINT_HEX(ctrlEndAddr);
    VAR_PRINT_HEX(pBuffAddr);
#endif
    return (ctrlEndAddr <= pBuffAddr) ? 0 : !0;
}

/**
 * @brief 创建控制块并初始化
 * @param[in] pool 内存池指针
 * @param[in] pBuff 缓冲区起始地址
 * @param[in] bufLen 缓冲区长度（包含头尾标记）
 * @return 成功返回控制块指针，失败返回 NULL
 */
static mgp_ctrl_t *mgp_createWithCtrlBlock(mgp_pool_t *pool, void *pBuff, const size_t bufLen)
{
    assert(pool);
    assert(pBuff);

    int idx = pool->allocBlockCnt;
    if (idx <= -1) // 初始索引不小于0
        return NULL;

    mgp_ctrl_t *newCtrl = &pool->ctrlTable[idx];
    if (mgp_MemoryCollisionCheck(newCtrl, pBuff)) // 检查控制块与缓冲区碰撞
        return NULL;

    pool->allocBlockCnt += 1;
    // idx = pool->allocBlockCnt - 1;
#if MGP_CTRL_BLOCK_DEBUG
    VAR_PRINT_INT(idx);
#endif

    pool->ctrlTable[idx].buffSize = bufLen;
#if MGP_MG_NUM
    pool->ctrlTable[idx].pHead = pBuff;
    pool->ctrlTable[idx].pTail = (void *)&((unsigned char *)pBuff)[bufLen - sizeof(unsigned int)];
    pool->ctrlTable[idx].pBuff = (void *)((mgp_ptr_t)pBuff + sizeof(unsigned int)); // tips: 注意缓冲区的位置位于head之后
#else
    pool->ctrlTable[idx].pBuff = pBuff;
#endif

#if MGP_MG_NUM
#if 0
    *(pool->ctrlTable[idx].pHead) = MGP_GUARD_HEAD;
    *(pool->ctrlTable[idx].pTail) = MGP_GUARD_TAIL;
#else
    const unsigned int head = MGP_GUARD_HEAD;
    const unsigned int tail = MGP_GUARD_TAIL;
    memcpy(pool->ctrlTable[idx].pHead, &head, sizeof(head));
    memcpy(pool->ctrlTable[idx].pTail, &tail, sizeof(tail));
#endif
#endif
#if MGP_CTRL_BLOCK_DEBUG
    VAR_PRINT_HEX((mgp_ptr_t)newCtrl);
    VAR_PRINT_HEX((mgp_ptr_t)&pool->ctrlTable[idx]);
#endif

    return &pool->ctrlTable[idx];
}

/**
 * @brief 获取空闲内存块
 * @param[in] pool 内存池指针
 * @param[in] bytes 需要的字节数
 * @return 成功返回空闲块指针，失败返回 NULL
 */
static void *mgp_getFreeBlock(mgp_pool_t *pool, const size_t bytes)
{
    assert(pool);
    const size_t actualSize = align_up(bytes, MGP_ALIGN_NUM); // 缓冲区实际大小
    void *ret = NULL, *temp_ptr = NULL;

    if (pool->allocBlockCnt < 1)
    { // 尚未申请块
        // 检查是否有足够的空间
        if (actualSize >= pool->totalSize)
            return NULL; // 空间不足，返回 NULL

// 当申请块小于 1, 直接从内存池顶部往下放
#if MGP_FREE_BLOCK_DEBUG
        VAR_PRINT_HEX((mgp_ptr_t)pool->baseAddr);
        VAR_PRINT_HEX((mgp_ptr_t)pool->totalSize);
        VAR_PRINT_HEX((mgp_ptr_t)actualSize);
        VAR_PRINT_HEX((mgp_ptr_t)pool->totalSize - actualSize);
        VAR_PRINT_HEX(((mgp_ptr_t)pool->baseAddr) + pool->totalSize - actualSize);
#endif
        ret = (void *)(((mgp_ptr_t)pool->baseAddr) + pool->totalSize - actualSize);
#if MGP_FREE_BLOCK_DEBUG
        VAR_PRINT_HEX((mgp_ptr_t)ret);
        VAR_PRINT_HEX((mgp_ptr_t)temp_ptr);
        DEBUG_PRINT("First block, allocating from top");
#endif
        goto _ret;
    }

    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompare);
#if MGP_FREE_BLOCK_DEBUG
    for (int ctrlIdx = 0; ctrlIdx < pool->allocBlockCnt;ctrlIdx++){
        DEBUG_PRINT("ctrlIdx[%d], pBuffAddr[%x], size[%u]",
                    ctrlIdx, (mgp_ptr_t)pool->ctrlTable[ctrlIdx].pBuff, pool->ctrlTable[ctrlIdx].buffSize);
    }
#endif

    int idx = 0;
#if MGP_MG_NUM
    mgp_ptr_t freeBlockEndAddr = ((mgp_ptr_t)pool->baseAddr) + pool->totalSize,                       // 指向内存池末端地址
        freeBlockStartAddr = ((mgp_ptr_t)pool->ctrlTable[idx].pHead) + pool->ctrlTable[idx].buffSize; // 此时已经将控制块表重排, 直接指向高位块的末端地址
#else
    mgp_ptr_t freeBlockEndAddr = ((mgp_ptr_t)pool->baseAddr) + pool->totalSize,                       // 指向内存池末端地址
        freeBlockStartAddr = ((mgp_ptr_t)pool->ctrlTable[idx].pBuff) + pool->ctrlTable[idx].buffSize; // 此时已经将控制块表重排, 直接指向高位块的末端地址
#endif
    mgp_ptr_t freeBytes = 0U;

    do
    {
        // 寻找合适的空闲块
        freeBytes = freeBlockEndAddr - freeBlockStartAddr; // 结果应当>0
#if MGP_FREE_BLOCK_DEBUG
        DEBUG_PRINT("free bytes = %u, idx = %d", freeBytes, idx);
#endif
        if (freeBytes >= actualSize)
        {
            // 此处是复用内存碎片, 不计入空闲空间大小
            ret = (void *)freeBlockStartAddr;
            return ret; // 直接返回该合适的块地址
        }
        if (freeBytes < 0) // 指针计算错误
            return NULL;
        idx++;
// 更新空闲块地址
#if MGP_MG_NUM
        freeBlockStartAddr = ((mgp_ptr_t)pool->ctrlTable[idx].pHead) + pool->ctrlTable[idx].buffSize;
        freeBlockEndAddr = ((mgp_ptr_t)pool->ctrlTable[idx - 1].pHead);
#else
        freeBlockStartAddr = ((mgp_ptr_t)pool->ctrlTable[idx].pBuff) + pool->ctrlTable[idx].buffSize;
        freeBlockEndAddr = ((mgp_ptr_t)pool->ctrlTable[idx - 1].pBuff);
#endif
    } while (idx < pool->allocBlockCnt);

// 所有块间隙中没有合适的空闲块，直接返回新的空闲块
// 但需要先检查是否有足够的空间
#if MGP_MG_NUM
    mgp_ptr_t lastBlockStart = (mgp_ptr_t)pool->ctrlTable[pool->allocBlockCnt - 1].pHead;
#else
    mgp_ptr_t lastBlockStart = (mgp_ptr_t)pool->ctrlTable[pool->allocBlockCnt - 1].pBuff;
#endif

    // 检查空间是否足够
    if (lastBlockStart < actualSize)
        return NULL; // 空间不足，返回 NULL

    ret = (void *)(lastBlockStart - ((mgp_ptr_t)actualSize));
#if MGP_FREE_BLOCK_DEBUG
    VAR_PRINT_HEX((mgp_ptr_t)ret);
#endif
_ret:
    // 额外检查：确保返回的地址在内存池内
    if ((mgp_ptr_t)ret < (mgp_ptr_t)pool->baseAddr)
        return NULL;

    // 指针对齐检查
    temp_ptr = align_ptr(ret, MGP_ALIGN_NUM);
    if (ret != temp_ptr)
        return NULL; // 缓冲区对齐后

    pool->freeSize -= actualSize;
    return ret;
}

/**
 * @brief 从内存池分配内存
 * @param[in] poolAddr 内存池句柄
 * @param[in] bytes 需要分配的字节数
 * @return 成功返回内存块指针，失败返回 NULL
 */
void *mgp_malloc(mgp_t poolAddr, const size_t bytes)
{
#if MGP_MEMORY_ALLOC_DEBUG
    assert(poolAddr);
#endif
    // 内存申请前先字节对齐, 申请出来的内存后边应当也是字节对齐
    const size_t totalBytes = (MGP_MG_NUM)
                                  ? (bytes + (sizeof(unsigned int) * 2))
                                  : (bytes);
    const size_t actualSize = align_up(totalBytes, MGP_ALIGN_NUM); // 缓冲区实际大小
    mgp_pool_t *p = poolAddr;

    // 先检查是否超过最大可分配尺寸（包含头尾标记）
    const size_t maxUserSize = mgp_canAllocMaxSize(p);
    if (totalBytes > maxUserSize)
        return NULL; // 超过最大可分配尺寸

    void *block = mgp_getFreeBlock(p, actualSize);
#if MGP_MEMORY_ALLOC_DEBUG
    VAR_PRINT_UD(totalBytes);
    VAR_PRINT_UD(actualSize);
    VAR_PRINT_HEX((mgp_ptr_t)block);
#endif
    if (!block)
        return NULL;

    mgp_ctrl_t *pCtrl = mgp_createWithCtrlBlock(p, block, actualSize);
    if (!pCtrl)
        return NULL;

#if MGP_MEMORY_ALLOC_DEBUG
    // 仅在 DEBUG 模式下打印日志
    DEBUG_PRINT("Allocated block at: %x, size: %u, alignSize: %u",
                (mgp_ptr_t)pCtrl->pBuff, (unsigned int)bytes, (unsigned int)pCtrl->buffSize);
#if MGP_MG_NUM
    DEBUG_PRINT("P->HEAD: %x, P->TAIL: %x", (mgp_ptr_t)pCtrl->pHead, (mgp_ptr_t)pCtrl->pTail);
#endif
#endif

    return pCtrl->pBuff;
}
#if MGP_MG_NUM
/**
 * @brief 检查内存块的头部和尾部魔术数
 * @param[in] ctrl 控制块指针
 * @return 返回值说明：
 *         - (-1): 参数错误（ctrl 为 NULL）
 *         - 0: 魔术数校验通过
 *         - 1: 魔术数不匹配（数据已损坏）
 */
static int mgp_magicNumHCheck(const mgp_ctrl_t *ctrl)
{
    if (ctrl)
        return MGP_ERR_ARG;

    if (*ctrl->pHead != MGP_GUARD_HEAD ||
        *ctrl->pTail != MGP_GUARD_TAIL)
    {
        return MGP_ERR_CORRUPT;
    }
    return MGP_OK;
}
#endif

/**
 * @brief 释放已分配的内存块
 * @param[in] poolAddr 内存池句柄
 * @param[in] p 要释放的内存块指针
 */
void mgp_free(mgp_t poolAddr, void *p)
{
    assert(poolAddr);
    assert(p);

    mgp_pool_t *pool = poolAddr;
    mgp_ctrl_t *ctrl = NULL;
    mgp_ctrl_t vCtrl = {
        .pBuff = p,
    };

#if MGP_MEMORY_FREE_DEBUG
    VAR_PRINT_HEX((mgp_ptr_t)vCtrl.pBuff);
#endif

    // ❌ 实际上释放时可能已经无序了（因为之前的释放操作打乱了顺序），必须先排序
    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompare);
    ctrl = bsearch(&vCtrl, pool->ctrlTable, pool->allocBlockCnt, sizeof(mgp_ctrl_t), mgp_ctrlBlockCompare);
    if (!ctrl)
    {
#if MGP_MEMORY_FREE_DEBUG
        ERROR_PRINT("This memory block is not in this memory pool.");
        return;
#endif
    }
#if MGP_MG_NUM
    ret = mgp_magicNumHCheck(ctrl);
    if (ctrl == MGP_ERR_CORRUPT)
    {
#if MGP_MEMORY_FREE_DEBUG
        ERROR_PRINT("The checksum has been corrupted. Please verify the data integrity.");
#endif
    }
#endif
    memset(ctrl, 0, sizeof(*ctrl));
    if (pool->allocBlockCnt > 1)
    {
        // 申请内存块数大于 1, 列表末端的控制块往前填充
        memcpy(ctrl, &pool->ctrlTable[pool->allocBlockCnt - 1], sizeof(*ctrl));
    }
    pool->allocBlockCnt -= 1;
#if MGP_MEMORY_FREE_DEBUG
    VAR_PRINT_INT(pool->allocBlockCnt);
#endif
    if (pool->allocBlockCnt < 0)
    { // 应该永远不会满足该条件
#if MGP_MEMORY_FREE_DEBUG
        ERROR_PRINT("ERROR pool->allocBlockCnt = %d", pool->allocBlockCnt);
#endif
    }
    return;
}

/**
 * @brief 重新分配内存块大小
 * @param[in] poolAddr 内存池句柄
 * @param[in] src 已分配的内存块指针
 * @param[in] bytes 需要重新分配的字节数
 * @return 成功返回新的内存块指针，失败返回 NULL
 */
void* mgp_realloc(mgp_t poolAddr, void* src, const size_t bytes){
    assert(poolAddr);
    if(!src)
        goto _malloc;

    const size_t totalBytes = (MGP_MG_NUM)
                                  ? (bytes + (sizeof(unsigned int) * 2))
                                  : (bytes);
    const size_t actualSize = align_up(totalBytes, MGP_ALIGN_NUM); // 缓冲区实际大小
    void *ret = NULL;
    mgp_pool_t *pool = poolAddr;
    mgp_ctrl_t *ctrl = NULL;
    mgp_ctrl_t vCtrl = {
        .buffSize = actualSize,
        .pBuff = src,
    };
    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompare);
    ctrl = bsearch(&vCtrl, pool->ctrlTable, pool->allocBlockCnt, sizeof(mgp_ctrl_t), mgp_ctrlBlockCompare); // 获取该内存块的控制块
    if(!ctrl) // 该内存块不在本内存池中
        goto _malloc;
    if(ctrl->buffSize >= vCtrl.buffSize) // 缓冲区长度足够长, 直接返回
        return src;

_malloc:
    ret = mgp_malloc(poolAddr, bytes);
    if(!ret) // 内存申请失败
        return ret;
    if(!src || !ctrl) // 无需对源内存块进行复制
        return ret;
#if MGP_MG_NUM
    memcpy(ret, ctrl->pBuff, ctrl->buffSize - sizeof(unsigned int)); // 魔术数字在创建控制块的时候已经被赋值
#else
    memcpy(ret, ctrl->pBuff, ctrl->buffSize);
#endif
    mgp_free(poolAddr, src);
    return ret;
}

#if MGP_DEBUG

/**
 * @brief 显示所有控制块信息（调试用）
 * @param[in] pool 内存池指针
 */
static void mgp_showCtrlBlocks(mgp_pool_t *pool)
{
    assert(pool);

    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
#if MGP_MG_NUM
        DEBUG_PRINT("addr[%d] = %p", i, pool->ctrlTable[i].pHead);
#else
        DEBUG_PRINT("addr[%d] = %x", i, (mgp_ptr_t)pool->ctrlTable[i].pBuff);
#endif
    }
}

/**
 * @brief 控制块测试函数（调试用）
 * @param[in] pool 内存池指针
 */
static void mgp_ctrlBlockTest(mgp_pool_t *pool)
{
    assert(pool);

    pool->allocBlockCnt = 8;
    srand(0x456);

    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
#if MGP_MG_NUM
        pool->ctrlTable[i].pHead = (void *)(rand() % 0x1000);
#else
        pool->ctrlTable[i].pBuff = (void *)(rand() % 0x1000);
#endif
    }

    mgp_showCtrlBlocks(pool);
    qsort(pool->ctrlTable, pool->allocBlockCnt, sizeof(*pool->ctrlTable), mgp_ctrlBlockCompare);
    mgp_showCtrlBlocks(pool);
}

/**
 * @brief 显示所有已分配的内存块信息
 * @param[in] pool 内存池指针
 */
void mgp_showAllocBlock(const mgp_pool_t *pool)
{
    assert(pool);
    if(pool->allocBlockCnt == 0){
        DEBUG_PRINT("NO ALLOC");
    }
#if 0
    return;
#endif
    for (int i = 0; i < pool->allocBlockCnt; i++)
    {
        DEBUG_PRINT("block[%d], size[%u] blockAddr[%x] | ctrlAddr[%x] ctrlBufAddr[%x]",
                    i, pool->ctrlTable[i].buffSize, (mgp_ptr_t)pool->ctrlTable[i].pBuff,
                    (mgp_ptr_t)&pool->ctrlTable[i], (mgp_ptr_t)&pool->ctrlTable[i].pBuff);
    }
}

/**
 * @brief 内存池综合测试函数
 * @param[in] p 内存池句柄
 */
void mgp_test(mgp_t p)
{
    assert(p);
    mgp_pool_t *pool = p;

    //mgp_ctrlBlockTest(pool);
    //return;

    void *mem1, *mem2, *mem3, *mem4;
    VAR_PRINT_UD(mgp_canAllocMaxSize(p));
    mem1 = mgp_malloc(p, 89);
    mem2 = mgp_malloc(p, 255);
    mem3 = mgp_malloc(p, 66);
    VAR_PRINT_HEX((mgp_ptr_t)mem1);
    VAR_PRINT_HEX((mgp_ptr_t)mem2);
    VAR_PRINT_HEX((mgp_ptr_t)mem3);

    mgp_showAllocBlock(p);
    VAR_PRINT_UD(mgp_canAllocMaxSize(p));
#if 0
    mgp_ctrl_t vCtrl = {
        .pBuff = mem2,
    };
    VAR_PRINT_INT(pool->allocBlockCnt);
    mgp_ctrl_t* pCtrl = bsearch(&vCtrl, pool->ctrlTable, pool->allocBlockCnt, sizeof(mgp_ctrl_t), mgp_ctrlBlockSearch);
    VAR_PRINT_HEX((mgp_ptr_t)pCtrl);
    if(!pCtrl)
        return;
    VAR_PRINT_HEX((mgp_ptr_t)pCtrl->pBuff);
    return;
#endif
    memcpy(mem1, "HELLO WORLD ! ! !", sizeof("HELLO WORLD ! ! !"));
    VAR_PRINT_STRING((char *)mem1);
    VAR_PRINT_ARR_HEX((unsigned char *)mem1, 32);

    mgp_free(p, mem1);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mem1 = mgp_malloc(p, 24);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mem4 = mgp_malloc(p, 32);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mgp_free(p, mem3);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mgp_free(p, mem2);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mgp_free(p, mem1);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    VAR_PRINT_HEX((mgp_ptr_t)mem1);
    VAR_PRINT_HEX((mgp_ptr_t)mem2);
    VAR_PRINT_HEX((mgp_ptr_t)mem3);

    mem1 = mgp_malloc(p, 64);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    mem1 = mgp_realloc(p, mem1, 128);
    mgp_showAllocBlock(p);
    DEBUG_PRINT("");

    return;

    VAR_PRINT_UD(align_up(66, 4));
    VAR_PRINT_UD(align_down(127, 4));
    VAR_PRINT_UD(align_up(66, 8));
    VAR_PRINT_UD(align_down(127, 8));
    mem1 = (void *)0x123989;
    VAR_PRINT_HEX((mgp_ptr_t)mem1);
    VAR_PRINT_HEX((mgp_ptr_t)align_ptr(mem1, 4));
    DEBUG_PRINT("");
    mgp_getFreeBlock(p, 63);
}
#endif
