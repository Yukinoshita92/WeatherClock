/**
 * @file    Int_AT24C02.c
 * @brief   AT24C02 EEPROM 读写驱动（I2C2，256 字节 / 8 字节一页）
 * @details 提供单字节与多字节两套读写接口，多字节写内部自动按页拆分：
 *            - 单字节：INT_AT24C02_Write_Byte / INT_AT24C02_Read_Byte
 *            - 多字节：INT_AT24C02_Write_Bytes / INT_AT24C02_Read_Bytes
 * @note    注意事项：
 *            - 多字节写不能跨页一次性下发，驱动已按页拆分，可直接传入任意长度；
 *            - 写入后需等待芯片内部完成，驱动内已延时 10ms；
 *            - 本驱动按 AT24C02 的地址与页大小实现，更换型号需同步修改。
 */
#include "Int_AT24C02.h"
/* 设备写地址：1010 0000 = 0xA0 */
#define AT24C02_ADDR_W  0xA0
/* 设备读地址：1010 0001 = 0xA1 */
#define AT24C02_ADDR_R  0xA1
#define BYTE_PER_PAGE  8    /* 一页 8 字节 */
#define ROM_CAPACITY   256  /* 总容量 256 字节 */

/* 单字节写入：data_addr 为 EEPROM 内部地址 */
void INT_AT24C02_Write_Byte(uint16_t data_addr,uint8_t data)
{
    /* HAL_I2C_Mem_Write 已封装完整写入时序：起始位 -> 设备地址 -> 存储器地址 -> 数据 -> 停止位 */
    /* 参数依次为：句柄、设备写地址、存储器地址、地址宽度、数据指针、数据长度、超时 */
    HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,data_addr,I2C_MEMADD_SIZE_8BIT,&data,1,100);
    HAL_Delay(10);/* 等待芯片内部写入完成（tWR 最大 5ms） */
}
/* 单字节读取：返回 data_addr 处的数据 */
uint8_t INT_AT24C02_Read_Byte(uint16_t data_addr)
{
    uint8_t data=0; 
    /* HAL_I2C_Mem_Read 已封装完整读取时序：起始位 -> 设备地址 -> 存储器地址 -> 重启 -> 读数据 -> 停止位 */
    /* 参数依次为：句柄、设备读地址、存储器地址、地址宽度、接收缓冲、读取长度、超时 */
    HAL_I2C_Mem_Read(&hi2c2,AT24C02_ADDR_R,data_addr,I2C_MEMADD_SIZE_8BIT,&data,1,100);
    return data;
}
/* 多字节写入：data_addr 为起始地址（写入时地址自动自增），num 为字节数
** 单次最多写一页（8 字节），超出部分由本函数按页拆分下发
*/
void INT_AT24C02_Write_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num)
{
    /* 越界检查：写入范围不得超过芯片容量 */
    if(data_addr+num>ROM_CAPACITY)
    {
        printf("[AT_24C02]CANNOT WRITE MORE THAN ROM CAPACITY\r\n");
        return;
    }
   
    /* 本次可连续写入的字节数 = 本页剩余空间 */
    uint16_t write_num=BYTE_PER_PAGE-data_addr%BYTE_PER_PAGE;

    /* 剩余数据仍在同一页内，一次写完 */
    if(write_num>=num)
    {
        HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,data_addr,I2C_MEMADD_SIZE_8BIT,data,num,1000);
        HAL_Delay(10);/* 等待芯片内部写入完成（tWR 最大 5ms） */
    }
    else/* 跨页：分多次写入 */
    {
         uint16_t data_offset=0;/* 已写入的数据偏移 */
         uint8_t page_start_addr=data_addr;/* 本页起始地址 */

         while(num>write_num)/* 按整页循环下发 */
         {
            /* 先补齐当前页的剩余空间，其余每页均写满 8 字节 */
            HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,page_start_addr,I2C_MEMADD_SIZE_8BIT,data+data_offset,write_num,1000);
            page_start_addr+=write_num;/* 推进到下一页起始地址 */
            data_offset+=write_num;/* 源数据指针随之后移 */
            num-=write_num;/* 剩余待写字节数 */
            write_num=BYTE_PER_PAGE;
            HAL_Delay(10);/* 等待芯片内部写入完成（tWR 最大 5ms） */
         }
         if(num>0)/* 末尾不足一页的剩余数据单独写入 */
         {
            HAL_I2C_Mem_Write(&hi2c2,AT24C02_ADDR_W,page_start_addr,I2C_MEMADD_SIZE_8BIT,data+data_offset,num,1000);
            HAL_Delay(10);/* 等待芯片内部写入完成（tWR 最大 5ms） */
         }
    }
}
/* 多字节读取：读取 data_addr 起的 num 字节（地址自动自增） */
void INT_AT24C02_Read_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num)
{
    /* 参数依次为：句柄、设备读地址、存储器地址、地址宽度、接收缓冲、读取长度、超时 */
    HAL_I2C_Mem_Read(&hi2c2,AT24C02_ADDR_R,data_addr,I2C_MEMADD_SIZE_8BIT,data,num,1000);
}

