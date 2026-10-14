/**
 * @file    Int_AT24C02.h
 * @brief   AT24C02 EEPROM 驱动接口（I2C，256B，8B/页）
 * @note    HAL 库配置完成后外设已初始化，无需手动初始化，本文件只封装读写函数。
 *          多字节读地址自动自增，可顺序读取多字节；但多字节写一次最多只能写 1 页
 *          （AT24C02 一页 8 字节），需注意页边界覆盖问题。
 *          本文件针对 AT24C02 实现，若更换其他型号 EEPROM，需修改对应的地址计算方式与页写长度限制。
 */
#ifndef __INT_AT24C02_H_
#define __INT_AT24C02_H_

#include <stdint.h>
#include <stdio.h>
#include "i2c.h"

/**
 * @brief  写 EEPROM 单字节
 * @param  data_addr 存储器地址
 * @param  data      要写入的数据
 */
void INT_AT24C02_Write_Byte(uint16_t data_addr,uint8_t data);

/**
 * @brief  读 EEPROM 单字节
 * @param  data_addr 存储器地址
 * @return 该地址中存储的数据
 */
uint8_t INT_AT24C02_Read_Byte(uint16_t data_addr);

/**
 * @brief  写 EEPROM 多字节
 * @param  data_addr 存储器起始地址（写入时地址自动自增）
 * @param  data      要写入的数据
 * @param  num       写入字节数
 */
void INT_AT24C02_Write_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num);

/**
 * @brief  读 EEPROM 多字节
 * @param  data_addr 存储器起始地址（读取时地址自动自增）
 * @param  data      接收缓冲区
 * @param  num       读取字节数
 */
void INT_AT24C02_Read_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num);

#endif //_INT_AT24C02_H_
