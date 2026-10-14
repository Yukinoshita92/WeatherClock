/**
 * @file    app.h
 * @brief   应用层各模块初始化接口
 */
#ifndef __APP_H
#define __APP_H

void board_init(void);          /* 板级初始化 */
void wifi_init(void);           /* ESP-AT 与 Wi-Fi 初始化 */
void wifi_wait_connect(void);   /* 等待 AP 连接成功（带超时） */
void mainloop_init(void);       /* 创建定时器并启动应用主循环 */

#endif
