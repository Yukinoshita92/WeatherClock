/**
 * @file    Int_AT24C02.c
 * @brief   AT24C02 EEPROM 驱动实现（I2C，256B，8B/页）
 * @note    HAL 库配置完成后外设已初始化，无需手动初始化，本文件只封装读写函数。
 *          多字节读地址自动自增，可顺序读取多字节；但多字节写一次最多只能写 1 页
 *          （AT24C02 一页 8 字节），需按页拆分并注意页边界覆盖问题。
 *          本文件针对 AT24C02 实现，若更换其他型号 EEPROM，需修改对应的地址计算方式与页写长度限制。
 */
#include "Int_AT24C02.h"

#define AT24C02_ADDR_W  0xA0
#define AT24C02_ADDR_R  0xA1
#define BYTE_PER_PAGE  8
#define ROM_CAPACITY   256

/**
 * @brief  写 EEPROM 单字节
 * @param  data_addr 存储器地址
 * @param  data      要写入的数据
 * @note   HAL_I2C_Mem_Write 内部已集成完整写时序
 *         （起始位 -> 设备地址 -> 等待应答 -> 存储器地址 -> 等待应答 -> 数据 -> 等待应答 -> 停止位）；
 *         写后需强制等待至少 5ms。
 */
void INT_AT24C02_Write_Byte(uint16_t data_addr,uint8_t data)
{
    HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,data_addr,I2C_MEMADD_SIZE_8BIT,&data,1,100);
    HAL_Delay(10);   /* 写入后强制等待（至少 5ms） */
}

/**
 * @brief  读 EEPROM 单字节
 * @param  data_addr 存储器地址
 * @return 该地址中存储的数据
 * @note   HAL_I2C_Mem_Read 内部已集成完整读时序（写地址定位 -> 重启 -> 读地址 -> 接收 -> NACK -> 停止位）。
 */
uint8_t INT_AT24C02_Read_Byte(uint16_t data_addr)
{
    uint8_t data=0;
    HAL_I2C_Mem_Read(&hi2c2,AT24C02_ADDR_R,data_addr,I2C_MEMADD_SIZE_8BIT,&data,1,100);
    return data;
}

/**
 * @brief  写 EEPROM 多字节
 * @param  data_addr 存储器起始地址（写入时地址自动自增）
 * @param  data      要写入的数据
 * @param  num       写入字节数
 * @note   单次最多写一页（8 字节）：不足 8 字节一次性写入，超过 8 字节按页分多次写入。
 */
void INT_AT24C02_Write_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num)
{
    /* 健壮性判断 */
    if(data_addr+num>ROM_CAPACITY)
    {
        printf("[AT_24C02]CANNOT WRITE MORE THAN ROM CAPACITY\r\n");
        return;
    }
    /* 当前页可写入的数量（非整页对齐时为上一页剩余空间） */
    uint16_t write_num=BYTE_PER_PAGE-data_addr%BYTE_PER_PAGE;

    if(write_num>=num)   /* 能一次性写完 */
    {
        HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,data_addr,I2C_MEMADD_SIZE_8BIT,data,num,1000);
        HAL_Delay(10);   /* 写入后强制等待（至少 5ms） */
    }
    else   /* 分多次写入 */
    {
         uint16_t data_offset=0;              /* 多次写入的数据偏移量 */
         uint8_t page_start_addr=data_addr;   /* 多次写入的页起始地址 */

         while(num>write_num)   /* 循环页写 */
         {
            /* 填满上一页剩余空间后，后续均为整页写入（不足一页时交由末尾分支处理） */
            HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,page_start_addr,I2C_MEMADD_SIZE_8BIT,data+data_offset,write_num,1000);
            page_start_addr+=write_num;   /* 下一页起始地址 */
            data_offset+=write_num;       /* 缓冲区数据偏移 */
            num-=write_num;               /* 剩余数据长度 */
            write_num=BYTE_PER_PAGE;
            HAL_Delay(10);   /* 写入后强制等待（至少 5ms） */
         }
         if(num>0)   /* 非页对齐：写入最后一页剩余数据 */
         {
            HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,page_start_addr,I2C_MEMADD_SIZE_8BIT,data+data_offset,num,1000);
            HAL_Delay(10);   /* 写入后强制等待（至少 5ms） */
         }
    }
}

/**
 * @brief  读 EEPROM 多字节
 * @param  data_addr 存储器起始地址（读取时地址自动自增）
 * @param  data      接收缓冲区
 * @param  num       读取字节数
 */
void INT_AT24C02_Read_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num)
{
    HAL_I2C_Mem_Read(&hi2c2,AT24C02_ADDR_R,data_addr,I2C_MEMADD_SIZE_8BIT,data,num,1000);
}
