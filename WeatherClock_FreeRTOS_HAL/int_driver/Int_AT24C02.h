/**
 * @file    Int_AT24C02.h
 * @brief   AT24C02 EEPROM（I2C）读写接口
 */
#ifndef __INT_AT24C02_H_
#define __INT_AT24C02_H_

#include <stdint.h>
#include <stdio.h>
#include "i2c.h"

/* 写EEPROM 单字节写入: 写进存储器的具体地址、要写入的数据 */
void INT_AT24C02_Write_Byte(uint16_t data_addr,uint8_t data);
/* 读EEPROM 单字节读取: 读取存储器具体地址中存储的数据、返回读取到的数据*/
uint8_t INT_AT24C02_Read_Byte(uint16_t data_addr);
/* 写EEPROM 多字节写入: 写进存储器的起始地址(写数据时地址自动自增)、要写入的数据、写入数据的数量 */
void INT_AT24C02_Write_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num);
/* 读EEPROM 多字节读取: 读取存储器的起始地址(读数据时地址自动自增)、存储读取数据的缓冲区、读取数据的数量 */
void INT_AT24C02_Read_Bytes(uint16_t data_addr,uint8_t *data,uint16_t num);

#endif /* _INT_AT24C02_H_ */
