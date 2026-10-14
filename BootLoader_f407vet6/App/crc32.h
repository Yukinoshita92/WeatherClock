/**
 * @file    crc32.h
 * @brief   软件 CRC32（整包固件完整性校验）
 * @details 对固件负载（以及 XBIN magic 头中记录的期望值）做 32 位校验，
 *          保证“下载 -> 写 Flash -> 回读”这条链路上不会悄悄丢/翻一个 bit。
 * @note    不使用 STM32 硬件 CRC 单元：其位序、是否反转、是否最终异或都与
 *          PC 端 zlib.crc32 对不上，两端永远算不出同一个值。本文件按 zlib
 *          算法实现，设备端与 PC 端结果必然一致。
 *          参数（与 zlib / 打包脚本一致）：
 *            poly       : 0x04C11DB7（表中使用反转值 0xEDB88320）
 *            init       : 0xFFFFFFFF
 *            final xor  : 0xFFFFFFFF
 *            reflect    : none（用反转多项式实现“低位优先”）
 *            self-check : crc32("123456789", 9) == 0xCBF43926   ← 标准自检向量
 */
#ifndef __CRC32_H__
#define __CRC32_H__

#include <stdint.h>

/**
 * @brief  计算 data 前 len 字节的 CRC32
 * @param  data 数据首地址
 * @param  len  数据长度
 * @return 最终的 CRC32 值（在头部/帧里以“小端”存放，低字节在前）
 */
uint32_t crc32(const uint8_t *data, uint32_t len);

#endif /* __CRC32_H__ */
