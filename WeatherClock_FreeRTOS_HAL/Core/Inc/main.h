/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ESP_TX_Pin GPIO_PIN_2
#define ESP_TX_GPIO_Port GPIOA
#define ESP_RX_Pin GPIO_PIN_3
#define ESP_RX_GPIO_Port GPIOA
#define AT24C02_SCL_Pin GPIO_PIN_10
#define AT24C02_SCL_GPIO_Port GPIOB
#define AT24C02_SDA_Pin GPIO_PIN_11
#define AT24C02_SDA_GPIO_Port GPIOB
#define St7789_CS_Pin GPIO_PIN_12
#define St7789_CS_GPIO_Port GPIOB
#define St7789_SCK_Pin GPIO_PIN_13
#define St7789_SCK_GPIO_Port GPIOB
#define St7789_RST_Pin GPIO_PIN_14
#define St7789_RST_GPIO_Port GPIOB
#define St7789_SDA_Pin GPIO_PIN_15
#define St7789_SDA_GPIO_Port GPIOB
#define St7789_DC_Pin GPIO_PIN_6
#define St7789_DC_GPIO_Port GPIOC
#define St7789_BL_Pin GPIO_PIN_7
#define St7789_BL_GPIO_Port GPIOC
#define W25Q64_CS_Pin GPIO_PIN_6
#define W25Q64_CS_GPIO_Port GPIOD
#define W25Q64_SCK_Pin GPIO_PIN_3
#define W25Q64_SCK_GPIO_Port GPIOB
#define W25Q64_DO_Pin GPIO_PIN_4
#define W25Q64_DO_GPIO_Port GPIOB
#define W25Q64_DI_Pin GPIO_PIN_5
#define W25Q64_DI_GPIO_Port GPIOB
#define aht20_SCL_Pin GPIO_PIN_8
#define aht20_SCL_GPIO_Port GPIOB
#define aht20_SDA_Pin GPIO_PIN_9
#define aht20_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
