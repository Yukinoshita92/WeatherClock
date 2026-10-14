/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_head.h"
#include "app.h"
#include "onenet_mqtt.h"    /* OneNET MQTT 接入层 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for Init_Task */
osThreadId_t Init_TaskHandle;
const osThreadAttr_t Init_Task_attributes = {
  .name = "Init_Task",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal5,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void main_init(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Init_Task */
  Init_TaskHandle = osThreadNew(main_init, NULL, &Init_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_main_init */
/**
  * @brief  Function implementing the Init_Task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_main_init */
void main_init(void *argument)
{
  /* USER CODE BEGIN main_init */

   /* Init task entry */
	/* Low-level board init */
	board_init();
	/* UI init */
	UI_Init();

	/* Welcome page */
	welcome_page_display();

	/* Wifi init */
	wifi_init();
	wifi_page_display(CFG_WIFI_SSID,"[Wifi] Connecting...");
	wifi_wait_connect();

	/* 接入 OneNET MQTT：失败打日志，后续由 HTTP 兜底承接 */
	if (!onenet_mqtt_init())
	{
		printf("[SYS] onenet mqtt init fail, fallback http\r\n");
	}

	/* when wifi success connect , init the ota task */

	/* Default page: main */
	main_page_display();

	/* Start periodic timers (jobs run on the workqueue) */
	mainloop_init();

  /* Finally init the ota task */
	ota_init();

	/* Self-delete: stack is static, release memory */
	vTaskDelete(NULL);

  /* Infinite loop */
  for(;;)
  {
    ;/* empty, never reached */
  }
  /* USER CODE END main_init */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

