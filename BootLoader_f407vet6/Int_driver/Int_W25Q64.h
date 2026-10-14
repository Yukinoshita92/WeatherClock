/**
 * @file    Int_W25Q64.h
 * @brief   W25Q64 SPI Flash 驱动接口（64Mbit / 8MByte）
 * @note    读数据、读 ID、读状态寄存器均无需写使能；写数据、写状态寄存器、擦除
 *          都需要先发送写使能指令。除读状态寄存器、读 ID 外，每次读写操作前都应
 *          判断器件是否正忙（写使能内部已含忙等待，可覆盖大部分场景）；
 *          每次操作结束务必拉高片选。
 */
#ifndef __INT_W25Q64_H_
#define __INT_W25Q64_H_

#include <stdint.h>
#include <stdio.h>
#include "spi.h"

/* W25Q64 指令集：读取ID 读取状态寄存器 读取数据 写入数据 擦除扇区 写使能 */
#define W25Q64_READ_ID 0x9F
#define W25Q64_READ_STATUS_REG 0x05
#define W25Q64_READ_DATA 0x03
#define W25Q64_WRITE_DATA 0x02
#define W25Q64_ERASE_SECTOR 0x20
#define W25Q64_WRITE_ENABLE 0X06


/**
 * @brief  将数据写入 W25Q64（按块/扇/页/页内地址定位）
 * @param  block  块号
 * @param  sector 扇区号
 * @param  page   页号
 * @param  addr   页内地址
 * @param  data   数据指针
 * @param  num    数据长度
 * @note   内部已按 256 字节页边界拆分为多次页写，传入的 num 可以跨页，应用层无需分次调用。
 */
void INT_W25Q64_Write(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num);

/**
 * @brief  将数据写入 W25Q64（直接传入 32 位地址）
 * @param  addr 24 位存储地址（不需要拆分块号/扇区号/页号/页内地址）
 * @param  data 数据指针
 * @param  num  数据长度
 * @note   功能与 INT_W25Q64_Write 相同，仅地址组织形式不同。
 */
void INT_W25Q64_Write_with_32addr(uint32_t addr,uint8_t *data,uint16_t num);

/**
 * @brief  从 W25Q64 读取数据（按块/扇/页/页内地址定位）
 * @param  block  块号
 * @param  sector 扇区号
 * @param  page   页号
 * @param  addr   页内地址
 * @param  data   接收缓冲区
 * @param  num    读取长度
 */
void INT_W25Q64_Read(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num);

/**
 * @brief  从 W25Q64 读取数据（直接传入 32 位地址）
 * @param  addr 24 位存储地址
 * @param  data 接收缓冲区
 * @param  num  读取长度
 * @note   功能与 INT_W25Q64_Read 相同，仅地址组织形式不同。
 */
void INT_W25Q64_Read_with_32addr(uint32_t addr,uint8_t *data,uint16_t num);

/**
 * @brief  擦除一个扇区（W25Q64 最小擦除单元为 1 扇 = 4KB）
 * @param  block  块号
 * @param  sector 扇区号
 */
void INT_W25Q64_Erase_Sector(uint8_t block,uint8_t sector);

/**
 * @brief  读取 W25Q64 的制造商 ID 和设备 ID
 * @param  mf_id  输出：制造商 ID
 * @param  dev_id 输出：设备 ID
 */
void INT_W25Q64_Get_ID(uint8_t *mf_id,uint16_t *dev_id);

#endif //_INT_W25Q64_H_
