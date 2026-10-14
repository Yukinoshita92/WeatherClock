/**
 * @file    byteorder.h
 * @brief   显式字节序编解码层（统一大端/小端读写，替代手写移位）
 * @details 串口升级协议（bl_packet.c）的帧头/长度/CRC16 均约定为“大端”，而 XBIN magic
 *          头的字段与 CRC32 为“小端”。此前手写移位（如 (x>>8)|(x&0xFF)）散落各处，
 *          可读性差且易错位（曾因字节序口径不一致导致 CRC 校验地址错位）。
 *          本文件提供显式字节序的 get/put 接口，让编解码与协议逻辑解耦，大小端一眼可辨。
 *
 *          命名约定：be = 大端(big-endian)，le = 小端(little-endian)；get = 读，put = 写。
 * @note    - 纯头文件、零依赖、static inline，可被 App 与 BootLoader 各自复制复用；
 *          - 按 uint8_t* 逐字节操作，隐式规避对齐问题：若写成 *(uint32_t*)p 再 htonl，
 *            在 Cortex-M 上遇到非对齐地址会直接 HardFault，且属严格别名违规；
 *          - 相对宏 / 普通函数：宏无类型检查且参数可能重复求值；普通函数需跨 .c 链接，
 *            还可能被复制到 App 与 BootLoader 两个工程。static inline + 纯头文件
 *            在每个编译单元内联展开，无符号冲突、无运行时开销，两工程各自拷贝即可复用。
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
