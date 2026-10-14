/**
 * @file    log.c
 * @brief   轻量分级日志框架实现
 * @details 负责拼接"[等级][TAG] "前缀并完成可变参数的格式化输出，同时维护输出后端：
 *          默认为 printf（已重定向到 USART1），可通过 log_set_output() 改用其他后端。
 * @note    注意事项：
 *            - 单条日志缓冲固定 160 字节，超长部分会被截断；
 *            - 若从未调用 log_init()，首次输出时会自动切换到默认后端。
 */
#include "log.h"
#include <stdarg.h>   /* 提供可变参数处理（va_list / va_start / va_end） */
#include <stdio.h>

/* 当前输出后端（NULL = 未初始化，首次使用时惰性切到默认 printf 后端） */
static log_output_fn s_output = NULL;

/* 默认后端：输出到串口（printf 已重定向到 USART1） */
static void log_default_output(const char *line)
{
    printf("%s", line);
}
/* 初始化：可省略，默认后端即为 printf（已重定向到 USART1） */
void log_init(void)
{
    if (s_output == NULL)
    {
        s_output = log_default_output;
    }
}

/* 设置输出后端；传 NULL 则恢复默认的 printf（串口）后端 */
void log_set_output(log_output_fn fn)
{
    s_output = (fn != NULL) ? fn : log_default_output;
}

/* 等级转单字符（E/W/I/D），用于日志前缀 */
static const char *log_level_str(int level)
{
    switch (level)
    {
        case LOG_LEVEL_ERROR: return "E";
        case LOG_LEVEL_WARN:  return "W";
        case LOG_LEVEL_INFO:  return "I";
        case LOG_LEVEL_DEBUG: return "D";
        default:              return "?";
    }
}

/**
 * @brief  核心日志格式化输出
 * @param  level 日志等级（ERROR/WARN/INFO/DEBUG）
 * @param  tag   模块 TAG
 * @param  fmt   用户格式化字符串
 * @note   先拼装 "[等级][TAG] " 前缀，再追加格式化后的用户消息，最后交由输出后端。
 */
void log_output(int level, const char *tag, const char *fmt, ...)
{
    char    buf[160];
    va_list args;   /* 可变参数游标 */
    int     len;

    if (s_output == NULL)
    {
        s_output = log_default_output;   /* 惰性初始化：未显式设置其它后端时使用 printf 后端 */
    }

    /* 拼装 [等级][TAG] 前缀 */
    len = snprintf(buf, sizeof(buf), "[%s][%s] ", log_level_str(level), tag);
    if (len < 0 || len >= (int)sizeof(buf))
    {
        return;   /* 前缀超长（不会发生），保险防御 */
    }

    /* 追加用户消息（超长自动截断，保证以 '\0' 结尾） */
    va_start(args, fmt);   /* 初始化可变参数列表 */
    /* 将用户 fmt 与可变参数列表按格式写入 buf 的前缀之后，最后统一交给输出后端 */
    vsnprintf(buf + len, sizeof(buf) - (size_t)len, fmt, args);
    va_end(args);

    s_output(buf);
}
