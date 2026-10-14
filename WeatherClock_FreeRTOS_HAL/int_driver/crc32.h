/**
 * @file    crc32.h
 * @brief   软件 CRC32 接口（查表法，与 zlib.crc32 一致）
 * @details 用于固件负载与 XBIN 头部的完整性校验，确保"下载 / 拷贝 / 回读"全链路
 *          不出现静默位错误。算法参数与 PC 端工具一致：
 *            多项式 0x04C11DB7（查表使用反转多项式 0xEDB88320）
 *            初值 0xFFFFFFFF，最终异或 0xFFFFFFFF，不反射
 * @note    注意事项：
 *            - 未使用 STM32 硬件 CRC：其位序与最终异或方式与 PC 端不同，两端无法比对；
 *            - 自检值：crc32("123456789", 9) == 0xCBF43926。
 */
#ifndef __CRC32_H__
#define __CRC32_H__

#include <stdint.h>

/* 计算 data[0..len-1] 的 CRC32，返回最终值（按小端存放） */
uint32_t crc32(const uint8_t *data, uint32_t len);

#endif /* __CRC32_H__ */
