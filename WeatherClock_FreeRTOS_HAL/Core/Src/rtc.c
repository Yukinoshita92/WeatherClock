/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */

/**
**@brief RTC init and clock enable
**@note  The standard-lib init is now done by the CubeMX RTC config; make sure
**       RTC Clock Mux selects LSE in the clock tree.
**@note  Note: HAL RTC init struct field names differ from the standard lib.
**@note  HAL reads both Time and Date; due to register latching, read Time
**       first, then Date.
**@note  Clock source is set up here so time is valid without a manual init.
*/

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* Time sync takes time; a read may cross midnight (00:00), so verify by reading twice */

/* Set time */
static void _rtc_set_time_once(const rtc_date_t *date_time)
{
    RTC_DateTypeDef date={0};/* system date/time struct */
    RTC_TimeTypeDef time={0};

    /* Fill the date/time structs to program the registers */
    date.Year=date_time->year-2000;/* HAL year field is 0~99: store year-2000, add 2000 back on read */
    date.WeekDay=date_time->weekday;
    date.Month=date_time->month;
    date.Date=date_time->day;
    time.Hours=date_time->hour;
    time.Minutes=date_time->minute;
    time.Seconds=date_time->second;

    /* Program the registers (write stores these values as the current time) */
    HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
}
/* Read time twice (second read confirms a stable value) */
static void _rtc_get_time_once(rtc_date_t *date_time)
{
    /* Date/time structs */
    RTC_DateTypeDef date={0};
    RTC_TimeTypeDef time={0};

    /* HAL GetTime/GetDate both fill the structs, so no init is needed */

    /* Read the register time: Time first, then Date (latch order matters) */
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    /* Copy into the app time struct */
    date_time->year = 2000 + date.Year;
    date_time->month = date.Month;
    date_time->day = date.Date;
    date_time->weekday = date.WeekDay;
    date_time->hour = time.Hours;
    date_time->minute = time.Minutes;
    date_time->second = time.Seconds;
}

/* Retry until the programmed second matches, i.e. the write took effect */
void rtc_set_time(const rtc_date_t *date_time)
{
    rtc_date_t now_time;
    do
    {
        _rtc_set_time_once(date_time);
        vTaskDelay(pdMS_TO_TICKS(5));
        _rtc_get_time_once(&now_time);
    }while(date_time->second!=now_time.second);/* compare the smallest unit (seconds): equal means the write took effect */
}
/* Read twice; if seconds match, the read is consistent */
void rtc_get_time(rtc_date_t *date_time)
{
    rtc_date_t now_time;
    do
    {
      _rtc_get_time_once(date_time);

      _rtc_get_time_once(&now_time);
    }while(date_time->second!=now_time.second);
}

/* USER CODE END 1 */

