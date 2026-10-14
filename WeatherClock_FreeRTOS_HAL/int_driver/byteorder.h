/**
 * @file    byteorder.h
 * @brief   显式字节序编解码（大端 / 小端，get / put）
 * @details 串口升级协议的帧头、长度与 CRC16 约定大端，XBIN 头部字段与 CRC32 约定
 *          小端；本文件以纯头文件 static inline 的方式提供读写函数，使编解码与协议
 *          逻辑解耦。命名约定：be = 大端、le = 小端；get = 读、put = 写。
 * @note    注意事项：
 *            - 一律按 uint8_t* 逐字节操作，不做指针类型转换，天然规避非对齐访问与
 *              严格别名问题；
 *            - 函数内联展开、无运行时开销，可由多个工程各自拷贝复用。
 */

#ifndef __BYTEORDER_H__
#define __BYTEORDER_H__

#include <stdint.h>

/* ================= 大端（网络序，串口帧协议用） ================= */

/** @brief 读 2 字节大端 -> uint16 */
static inline uint16_t get_u16_be(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/** @brief 读 4 字节大端 -> uint32 */
static inline uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/** @brief 写 uint16 -> 2 字节大端（p 为存放容器，v 为源 16 位数据） */
static inline void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

/** @brief 写 uint32 -> 4 字节大端 */
static inline void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFFu);
}

/* ================= 小端（CRC32 / XBIN magic 字段用） ================= */

/** @brief 读 2 字节小端 -> uint16 */
static inline uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** @brief 读 4 字节小端 -> uint32 */
static inline uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief 写 uint16 -> 2 字节小端 */
static inline void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/** @brief 写 uint32 -> 4 字节小端 */
static inline void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#endif /* __BYTEORDER_H__ */
