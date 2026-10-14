/**
 * @file    tim_delay.h
 * @brief   基于 TIM6 的微秒/毫秒级时间戳与延时接口（备用时基）
 */
#ifndef __TIM_DELAY_H__
#define __TIM_DELAY_H__

#include <stdint.h>
#include <string.h>

/* 周期回调函数原型（由 tim_delay_period_elapsed 触发） */
typedef void (*tim_periodic_callback_t)(void);

uint64_t tim_now(void);                                              /* 读取当前微秒级时间戳 */
uint64_t tim_get_us(void);                                           /* 获取微秒时间戳 */
uint64_t tim_get_ms(void);                                           /* 获取毫秒时间戳 */
void tim_delay_us(uint32_t us);                                      /* 阻塞延时（微秒） */
void tim_delay_ms(uint32_t ms);                                      /* 阻塞延时（毫秒） */
void tim_register_periodic_callback(tim_periodic_callback_t callback);/* 注册周期回调 */
void tim_delay_period_elapsed(void);                                 /* 周期到达接口，由 TIM6 中断调用 */

#endif /* __TIM_DELAY_H__ */
