/**
 * @file    app_head.h
 * @brief   应用层公共头：汇总各模块头文件与全局宏
 */
#ifndef __APP_HEAD_H
#define __APP_HEAD_H

#include <stdint.h>
#include <stdio.h>
#include "app.h"
#include "aht20.h"
#include "usart.h"
#include "UI.h"
#include "esp_at.h"
#include "page.h"
#include "workqueue.h"
#include "ota.h"

#include "app_config.h"      /* 配置收口：SSID/PWD/天气Key 等统一由 app_config.h 提供 */

#endif
