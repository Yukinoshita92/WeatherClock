/**
 * @file    log.h
 * @brief   轻量分级日志框架（编译期裁剪 + 按模块 TAG）
 * @details 提供 ERROR / WARN / INFO / DEBUG 四级日志，低于 LOG_GLOBAL_LEVEL 的语句
 *          在编译期展开为空，不占 Flash 与 CPU；输出后端可插拔，默认为 printf。
 *          用法：在 .c 文件顶部先 #define LOG_TAG "模块名"，再 #include "log.h"。
 * @note    注意事项：
 *            - LOG_TAG 必须在 #include "log.h" 之前定义，否则使用默认 TAG；
 *            - 发布版本可调高 LOG_GLOBAL_LEVEL 以减小体积；
 *            - 默认后端为阻塞式串口输出，高频日志建议改用 DMA/中断异步后端。
 */
#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

/* ================= 日志等级（数值越大越详细） ================= */
#define LOG_LEVEL_NONE  0   /* 关闭所有日志 */
#define LOG_LEVEL_ERROR 1   /* 错误：影响功能 */
#define LOG_LEVEL_WARN  2   /* 警告：异常但可继续 */
#define LOG_LEVEL_INFO  3   /* 信息：关注流程节点 */
#define LOG_LEVEL_DEBUG 4   /* 调试：细节/十六进制等 */

/* 全局日志等级（编译期裁剪阈值）。开发=DEBUG，正式发布用 INFO/WARN 减小体积 默认为 DEBUG */
#ifndef LOG_GLOBAL_LEVEL
#define LOG_GLOBAL_LEVEL LOG_LEVEL_DEBUG
#endif

/* 模块 TAG：各 .c 中 include 前 #define LOG_TAG "XXX"，未定义则用默认 */
#ifndef LOG_TAG
#define LOG_TAG "APP"
#endif

/* ================= 输出后端抽象（后续可改为 DMA/中断异步发送） ================= */
/** @brief 日志输出后端函数类型：上层把整条日志字符串交给后端输出 */
typedef void (*log_output_fn)(const char *line);

void log_init(void);                       /* 初始化（可省略，未显式调用自动用 printf 后端） */
void log_set_output(log_output_fn fn);     /* 设置输出后端（注册回调，设置调试输出函数），传 NULL 恢复默认 printf */
void log_output(int level, const char *tag, const char *fmt, ...); /* 底层输出（内部使用） */

/* ================= 等级宏（编译期裁剪，低于对应等级的展开为空） ================= */

/* 若 log 等级比错误日志高，则正常打印，否则将 LOG_E 替换为空 */
#if LOG_GLOBAL_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(...) log_output(LOG_LEVEL_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOG_E(...) ((void)0)
#endif

/* 若 log 等级比警告日志高，则正常打印，否则将 LOG_W 替换为空 */
#if LOG_GLOBAL_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(...) log_output(LOG_LEVEL_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOG_W(...) ((void)0)
#endif

/* 若 log 等级比信息日志高，则正常打印，否则将 LOG_I 替换为空 */
#if LOG_GLOBAL_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(...) log_output(LOG_LEVEL_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOG_I(...) ((void)0)
#endif

/* 若 log 等级比调试日志高，则正常打印，否则将 LOG_D 替换为空 */
#if LOG_GLOBAL_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(...) log_output(LOG_LEVEL_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#define LOG_D(...) ((void)0)
#endif

/**
 * @note  等级宏说明：
 *        1) 低于阈值的等级被展开为 ((void)0) 而非留空，用于丢弃实参并避免编译告警；
 *        2) 生效时展开为 log_output(等级, TAG, ...)，... 即可变参数，经 __VA_ARGS__
 *           原样转发给 log_output（相当于带 TAG 前缀的 printf）。
 */

#endif /* __LOG_H__ */
