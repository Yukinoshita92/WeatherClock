/**
 * @file    Int_bootloader_skip.c
 * @brief   BootLoader 跳转到 App 的实现
 * @details 本文件从原串口 BL 逻辑中剥离，只保留“跳转到 App”部分。原完整逻辑为：
 *          1) 串口交互等待指令；2) 接收固件；3) 擦写 Flash；4) 简单校验；
 *          5) 跳转到 App。其余逻辑已全部移除，本文件可直接放入 BL 逻辑中使用。
 *
 *          跳转步骤：
 *            1) 校验栈顶地址、复位中断向量；
 *            2) 注销 BL（关闭中断、重置 HAL 库配置、重置 SysTick），把环境清理干净；
 *            3) 重新设置栈顶指针为 App 的栈顶地址；
 *            4) 重定向向量表（修改 VTOR）；
 *            5) 取出复位中断指针并直接调用（跳转到 App）。
 * @note    - 目标为 F407VET6（512KB Flash）；
 *          - 分区必须恰好落在 App 起始地址上：F103 按 2KB 页对齐，而 F407 扇区大小不一，
 *            必须按实际扇区边界对齐。
 */

#include "int_bootloader_skip.h"

/**
 * @brief  跳转到 App 应用程序
 * @param  app_start_addr App 起始地址
 * @return true 表示跳转成功；false 表示栈顶地址或复位向量校验失败
 * @note   函数指针承接复位中断函数地址，随后调用即完成跳转。
 */
bool Int_bootloader_jump_to_app(uint32_t app_start_addr)
{
    typedef void (*reset_handler_t)(void);

    /* 校验两个关键数据：栈顶地址、复位中断地址 */
    uint32_t stack_top_addr=*(volatile uint32_t*)app_start_addr;
    uint32_t reset_handler_addr=*(volatile uint32_t*)(app_start_addr+4);

    if(stack_top_addr<SRAM_START_ADDR || stack_top_addr>SRAM_END_ADDR)
    {
        printf("stack_top_addr error\n");
        return false;
    }
    if(reset_handler_addr<app_start_addr || reset_handler_addr>APP_END_ADDR)
    {
        printf("reset_handler_addr error\n");
        return false;
    }

    /* 注销 BL */
    /* HAL_DeInit 只重置外设配置，中断归内核管理，需手动关闭外设中断 */
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(EXTI2_IRQn);   /* 按键外部中断 */
    SysTick->CTRL=0;
    SysTick->LOAD=0;
    SysTick->VAL=0;
    HAL_DeInit();
    __disable_irq();

    /* 重新设置栈顶指针为 App 的栈顶地址 */
    __set_MSP(stack_top_addr);

    /* 重定向向量表（修改 VTOR） */
    SCB->VTOR = app_start_addr;

    /* 取出复位中断指针并直接调用（跳转到 App） */
    reset_handler_t jump_to_app =(reset_handler_t) reset_handler_addr;
    jump_to_app();

    return true;
}

