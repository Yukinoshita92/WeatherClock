/**
 * @file    tim_delay.c
 * @brief   基于 TIM6 的微秒/毫秒级时间戳与阻塞延时
 * @details 用 TIM6 溢出中断累加高位计数，配合计数器寄存器拼出微秒级时间戳，
 *          并据此实现 tim_delay_us() / tim_delay_ms()；另支持注册周期回调，
 *          由 tim_delay_period_elapsed() 在每次中断中触发。
 * @note    注意事项：
 *            - 使用前需在 main() 中调用 HAL_TIM_Base_Start_IT(&htim6) 启动时基；
 *            - 延时为阻塞式，不会让出 CPU，RTOS 任务中请优先使用 vTaskDelay；
 *            - 时间戳由 TIM6 的溢出周期决定最大可连续计时长度。
 */
#include "tim.h"
#include "tim_delay.h"

static volatile uint64_t tim_tick_count;                  /* TIM6 溢出累计的微秒数 */
static tim_periodic_callback_t periodic_callback;         /* 周期回调函数指针 */


/* 读取微秒级时间戳：高位用累计值、低位用 TIM6 计数器拼接，避免读过程被溢出打断 */
uint64_t tim_now(void)
{
    uint64_t now, last_count;
    do {
        last_count = tim_tick_count;
        now = tim_tick_count + TIM6->CNT;
    } while (last_count != tim_tick_count);
    return now;
}

/* 获取微秒级时间戳 */
uint64_t tim_get_us(void)
{
    return tim_now();
}

/* 获取毫秒级时间戳 */
uint64_t tim_get_ms(void)
{
    return tim_now() / 1000;
}

/* 阻塞式延时（单位：微秒） */
void tim_delay_us(uint32_t us)
{
    uint64_t now = tim_now();
    while (tim_now() - now < (uint64_t)us);
}

/* 阻塞式延时（单位：毫秒） */
void tim_delay_ms(uint32_t ms)
{
    uint64_t now = tim_now();
    while (tim_now() - now < (uint64_t)ms * 1000);
}

/* 注册周期回调（由 tim_delay_period_elapsed 触发） */
void tim_register_periodic_callback(tim_periodic_callback_t callback)
{
    periodic_callback = callback;
}

/* 周期到达接口：由 TIM6 周期中断回调调用 */
void tim_delay_period_elapsed(void)
{
    tim_tick_count += 1000;
    if (periodic_callback)
        periodic_callback();
}

