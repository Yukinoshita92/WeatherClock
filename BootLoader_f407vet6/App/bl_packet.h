/**
 * @file    bl_packet.h
 * @brief   串口升级报文协议 - BL 与 PC 工具之间的“对话语言”
 * @details 定义 BootLoader 的有线烧录协议：PC 以帧为单位发送固件，BL 逐字节解析，
 *          校验 CRC16，执行擦除/烧录/校验/跳转，并对每一帧给出应答。
 *
 *          这是最后的兜底恢复通道：当 OTA 失败、或 A/B 两槽均损坏时，
 *          仍可通过 PC 串口链路把设备救回。
 */
#ifndef __BL_PACKET_H__
#define __BL_PACKET_H__

#include <stdint.h>

/** @brief 初始化报文解析器（进入 BL 时调用一次） */
void bl_packet_init(void);

/**
 * @brief  核心状态机（循环执行）
 * @param  byte 喂入的 1 个字节（来自串口接收中断 / 环形缓冲区）
 * @return 1 表示刚收到一帧并已完成解析、执行与应答；0 表示仍在接收中
 * @note   每喂入一个字节即递增索引，并根据索引推进状态机的每一步。
 */
int bl_packet_feed(uint8_t byte);

#endif /* __BL_PACKET_H__ */
