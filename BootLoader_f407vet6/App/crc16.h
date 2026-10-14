/**
 * @file    crc16.h
 * @brief   软件 CRC16（XMODEM 标准，串口升级协议的逐帧校验）
 * @details 串口升级把固件切成很多帧，CRC16 用于发现传输中损坏的帧：
 *          一旦校验不过就整帧丢弃，由上位机重发。
 * @note    参数必须与 PC 端工具 SerialUpgrader 的 CRC.cs 完全一致，
 *          否则每帧的校验都会失败：
 *            Name       : CRC-16/XMODEM (aka ZMODEM / CRC-16/ACORN)
 *            poly       : 0x1021        多项式
 *            init       : 0x0000        初值
 *            reflect    : none          输入/输出都不反转
 *            final xor  : 0x0000        结果不异或
 *            self-check : crc16("123456789", 9) == 0x31C3   ← 标准自检向量
 */
#ifndef __CRC16_H__
#define __CRC16_H__

#include <stdint.h>

/**
 * @brief  计算 buf 前 len 字节的 CRC16
 * @param  buf 数据首地址
 * @param  len 数据长度
 * @return 16 位 CRC 值（在帧里以“大端”存放，高字节在前）
 */
uint16_t crc16(const uint8_t *buf, uint32_t len);

#endif /* __CRC16_H__ */
