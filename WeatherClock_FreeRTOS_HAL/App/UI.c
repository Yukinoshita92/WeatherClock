/**
 * @file    UI.c
 * @brief   显示绘制层：把各页面的绘制请求打包成消息，由 UI 任务串行执行
 * @note    本模块日志 TAG = "UI"
 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"   /* 队列 API（xQueueCreate / xQueueSend / xQueueReceive） */
#include "cmsis_os.h"
#include "st7789.h"
#include "UI.h"

/* 分级日志框架：本模块 TAG=UI */
#define LOG_TAG "UI"
#include "log.h"

/* 绘制动作枚举：写队列时记录操作类型，UI 任务读队列后据此分发 */
typedef enum
{
	UI_ACTION_PRINT_COLOURS,
	UI_ACTION_WRITE_STRING,
	UI_ACTION_PRINT_IMAGE,
}UI_Draw_Action_t;

/* UI 消息：动作类型 + 该动作所需的参数（联合体，按 action 解释） */
typedef struct
{
	UI_Draw_Action_t action;
	union
	{
		struct 
		{
			uint16_t x1;
			uint16_t y1;
			uint16_t x2;
			uint16_t y2;
			uint16_t colours;
		}print_colours;
		struct 
		{
			uint16_t x;
			uint16_t y;
			const char *str;
			uint16_t colour;
			uint16_t bg_colour;
			const font_t *font;
		}write_string;
		struct 
		{
			uint16_t x;
			uint16_t y;
			const image_t *image;
		}print_image;
	}params_of_action;   /* 具名联合体成员（Keil 不支持匿名联合体） */
}UI_Msg_t;


static QueueHandle_t UI_QueueHandle;   /* UI 消息队列句柄 */

/* UI 任务：先初始化 ST7789，随后循环取队列并分发到对应绘制接口 */
void UI_Task(void* param)
{
	st7789_init();   /* 显示初始化放在本任务中执行，避免与 AHT20 等同步流程互相阻塞 */
	
	UI_Msg_t msg;
	
	/* 循环等待并执行绘制请求 */
	while(1)
	{
		/* 阻塞读队列，直到有新的绘制请求 */
		xQueueReceive(UI_QueueHandle,&msg,portMAX_DELAY);   /* 队列句柄 / 数据容器 / 等待时间 */
		
		switch(msg.action)
		{
		case UI_ACTION_PRINT_COLOURS:
			st7789_print_color(msg.params_of_action.print_colours.x1,msg.params_of_action.print_colours.y1,
								 msg.params_of_action.print_colours.x2,msg.params_of_action.print_colours.y2,
								 msg.params_of_action.print_colours.colours);					 
			break;
		case UI_ACTION_WRITE_STRING:
			st7789_write_string(msg.params_of_action.write_string.x,msg.params_of_action.write_string.y,
								 msg.params_of_action.write_string.str,
								 msg.params_of_action.write_string.colour, msg.params_of_action.write_string.bg_colour,
								 msg.params_of_action.write_string.font);
			/* 绘制完成后释放字符串占用的堆内存 */
			vPortFree((void*)msg.params_of_action.write_string.str);
			break;
		case UI_ACTION_PRINT_IMAGE:
			st7789_print_image(msg.params_of_action.print_image.x,msg.params_of_action.print_image.y,msg.params_of_action.print_image.image);
			break;
		default:
			LOG_W("Unknown UI action: %d\n",msg.action);
		break;
		}
	}
}

/* UI 模块初始化：创建消息队列并启动 UI 任务 */
void UI_Init(void)
{
	LOG_D("Free Heap: %u, Min Free Heap: %u\n", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());   /* 堆水位（调试用） */
	
	/* 创建队列 */
	UI_QueueHandle=xQueueCreate(16,sizeof(UI_Msg_t));   /* 16 个消息槽，每槽一个 UI_Msg_t */
	configASSERT(UI_QueueHandle);
	
	/* 创建任务 */
	xTaskCreate(UI_Task,"UI",2048,NULL,osPriorityNormal,NULL);
}

/* 以下三个接口封装同一套“填充消息 + 入队”流程 */
void UI_print_color(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t colours)
{
	UI_Msg_t msg;
	msg.action=UI_ACTION_PRINT_COLOURS;
	msg.params_of_action.print_colours.x1=x1;
	msg.params_of_action.print_colours.x2=x2;
	msg.params_of_action.print_colours.y1=y1;
	msg.params_of_action.print_colours.y2=y2;
	msg.params_of_action.print_colours.colours=colours;
	
	/* 入队（阻塞等待） */
	xQueueSend(UI_QueueHandle,&msg,portMAX_DELAY);   /* 队列满时调用者会被永久阻塞 */
}
/** @brief 入队一条字符串绘制请求（字符串会拷贝到堆，交由 UI 任务异步绘制） */
void UI_write_string(uint16_t x,uint16_t y,const char *str,uint16_t colour,uint16_t bg_colour,const font_t *font)
{
	/* 字符串需拷贝到堆上：入队后队列须独立持有，不能引用调用方的临时缓冲 */
	char* pstr=pvPortMalloc(strlen(str)+1);   /* +1 预留字符串结束符 */
	if (pstr == NULL)
    {
        LOG_E("ui write string malloc failed: %s\n", str);
		LOG_D("Free Heap: %u, Min Free Heap: %u\n", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());   /* 堆水位（调试用） */
        return;
    }
	strcpy(pstr,str);
	
	UI_Msg_t msg;
	msg.action=UI_ACTION_WRITE_STRING;
	msg.params_of_action.write_string.x=x;
	msg.params_of_action.write_string.y=y;
	msg.params_of_action.write_string.str=pstr;
	msg.params_of_action.write_string.colour=colour;
	msg.params_of_action.write_string.bg_colour=bg_colour;
	msg.params_of_action.write_string.font=font;

	/* 入队（阻塞等待） */
	xQueueSend(UI_QueueHandle,&msg,portMAX_DELAY);   /* 队列满时调用者会被永久阻塞 */
}
/** @brief 入队一条图片绘制请求，交由 UI 任务异步绘制 */
void UI_print_image(uint16_t x,uint16_t y,const image_t *image)
{
	UI_Msg_t msg;
	msg.action=UI_ACTION_PRINT_IMAGE;
	msg.params_of_action.print_image.x=x;
	msg.params_of_action.print_image.y=y;
	msg.params_of_action.print_image.image=image;

	/* 入队（阻塞等待） */
	xQueueSend(UI_QueueHandle,&msg,portMAX_DELAY);   /* 队列满时调用者会被永久阻塞 */
}
