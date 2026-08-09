/**
 * @file memGroundP.h
 * @brief 内存池管理模块头文件
 * @details 提供一个简单的静态内存池管理实现，支持内存块的分配和释放
 */

#pragma once
#ifndef _MEMGROUNDP_H_
#define _MEMGROUNDP_H_

#include <stdint.h>

#define MGP_DEBUG 1
#define MGP_MG_NUM 0

#define MGP_ALIGN_NUM 4

/** @defgroup MGP_CONFIG 配置选项 */
/** @defgroup MGP_TYPE 类型定义 */
/** @defgroup MGP_FUNC 函数接口 */
/** @defgroup MGP_CONST 常量定义 */

/* ==================== 保护标记值 ==================== */
/** @addtogroup MGP_CONST
 *  @{
 */
#if MGP_MG_NUM
#define MGP_GUARD_HEAD       0xDEADBEEFU    ///< 头部保护标记，用于检测内存块头部是否被破坏
#define MGP_GUARD_TAIL       0xCAFEBABEU    ///< 尾部保护标记，用于检测内存块尾部是否被破坏
#endif
/**  @} */

/* ==================== 错误码定义 ==================== */
/** @addtogroup MGP_CONST
 *  @{
 */
#define MGP_OK               0               ///< 成功
#define MGP_ERR_ARG          (-1)            ///< 参数错误
#define MGP_ERR_ALIGN        (-2)            ///< 对齐错误
#define MGP_ERR_NOMEM        (-3)            ///< 内存不足
#define MGP_ERR_CORRUPT      (-10)           ///< 检验损坏
#define MGP_ERR_NOT_INIT     (-20)           ///< 未初始化
/**  @} */

/** @addtogroup MGP_TYPE
 *  @{
 */
/**
 * @typedef mgp_t
 * @brief 内存池句柄类型
 * @details 指向内存池的指针，用于标识和管理一个内存池实例
 */
typedef void *mgp_t;
/**  @} */

/** @addtogroup MGP_FUNC
 *  @{
 */
/**
 * @brief 使用给定的内存区域创建内存池
 * @param[in] mem 指向预分配的内存区域起始地址
 * @param[in] bytes 内存区域的总大小（字节）
 * @return 成功返回内存池句柄，失败返回 NULL
 * @details 
 * - 初始化内存池控制结构
 * - 清空整个内存区域
 * - 最小内存需求：sizeof(mgp_pool_t) + sizeof(mgp_ctrl_t)
 * @note 该函数会清空传入的内存区域
 */
mgp_t mgp_create_with_pool(void *mem, size_t bytes);

/**
 * @brief 获取内存池可分配的最大内存块大小
 * @param[in] p 内存池句柄
 * @return 可分配的最大字节数
 * @details 计算时需减去内存池控制结构和控制块结构的开销
 */
const size_t mgp_canAllocMaxSize(mgp_t p);

/**
 * @brief 从内存池中分配指定大小的内存块
 * @param[in] poolAddr 内存池句柄
 * @param[in] bytes 需要分配的字节数
 * @return 成功返回内存块指针，失败返回 NULL
 * @details 
 * - 实际分配大小为 bytes + 2*sizeof(unsigned int)（用于头尾保护标记）
 * - 自动寻找合适的空闲块进行分配
 * - 支持内存碎片整理
 * @warning 返回的指针需要用户手动调用 mgp_free 释放
 */
void *mgp_malloc(mgp_t poolAddr, const size_t bytes);

/**
 * @brief 释放已分配的内存块
 * @param[in] poolAddr 内存池句柄
 * @param[in] p 要释放的内存块指针
 * @details 
 * - 查找对应的控制块并清空
 * - 自动整理控制块数组保持紧凑
 * - 如果释放非本池分配的地址会报错
 */
void mgp_free(mgp_t poolAddr, void *p);

/**
 * @brief 重新分配内存块大小
 * @param[in] poolAddr 内存池句柄
 * @param[in] src 已分配的内存块指针
 * @param[in] bytes 需要重新分配的字节数
 * @return 成功返回新的内存块指针，失败返回 NULL
 * @details 
 * - 如果 src 为 NULL，等同于 mgp_malloc
 * - 如果 bytes 小于等于原块大小，返回原指针且不释放
 * - 否则分配新块并复制原数据，然后自动释放原块
 * - 新块地址可能与原地址相同或不同
 * @note 原指针在函数内部会被自动释放，无需手动调用 mgp_free
 */
void *mgp_realloc(mgp_t poolAddr, void *src, const size_t bytes);

#if MGP_DEBUG
/**
 * @brief 内存池测试函数
 * @param[in] p 内存池句柄
 * @details 执行一系列分配和释放操作以验证内存池功能
 * @note 仅用于测试目的，生产环境不应调用
 */
void mgp_test(mgp_t p);

/**
 * @brief 运行所有测试用例
 * @details 一键执行所有定义的测试用例并输出统计报告
 *        包含基础功能、边界条件、压力测试等共 21 项测试
 */
void mgp_run_all_tests(void);
#endif
/**  @} */

#endif


