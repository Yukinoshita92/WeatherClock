/**
 * @file    Int_W25Q64.h
 * @brief   W25Q64 SPI Flash（64Mbit / 8MB）驱动接口
 */
#ifndef __INT_W25Q64_H_
#define __INT_W25Q64_H_

#include <stdint.h>
#include <stdio.h>
#include "spi.h"

/* W25Q64 指令集：读取ID / 读状态寄存器 / 读数据 / 写数据 / 擦除扇区 / 写使能 */
#define W25Q64_READ_ID 0x9F
#define W25Q64_READ_STATUS_REG 0x05
#define W25Q64_READ_DATA 0x03
#define W25Q64_WRITE_DATA 0x02
#define W25Q64_ERASE_SECTOR 0x20
#define W25Q64_WRITE_ENABLE 0X06


/* 将数据写入W25Q64：block:块号 sector:扇区号 page:页号 addr:页内地址 data:数据指针 num:数据长度
 * 内部已按256字节页边界拆分为多次页写 传入的num可以跨页 无需应用层分次调用 */
void INT_W25Q64_Write(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num);
/* 与 INT_W25Q64_Write 功能相同，但直接传入 32 位地址 */
void INT_W25Q64_Write_with_32addr(uint32_t addr,uint8_t *data,uint16_t num);

/* 从W25Q64中读取数据：block:块号 sector:扇区号 page:页号 addr:页内地址 data:数据指针 num:数据长度*/
void INT_W25Q64_Read(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num);
/* 与 INT_W25Q64_Read 功能相同，但直接传入 32 位地址 */
void INT_W25Q64_Read_with_32addr(uint32_t addr,uint8_t *data,uint16_t num);

/* W25Q64擦除最小单元是1扇 4kb ：block:块号 sector:扇区号*/
void INT_W25Q64_Erase_Sector(uint8_t block,uint8_t sector);

/* 使用指令读取W25Q64的制造商ID和设备ID */
void INT_W25Q64_Get_ID(uint8_t *mf_id,uint16_t *dev_id);

#endif /* _INT_W25Q64_H_ */
