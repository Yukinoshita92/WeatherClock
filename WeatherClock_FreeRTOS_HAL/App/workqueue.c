/**
 * @file    workqueue.c
 * @brief   轻量工作队列：把“函数 + 参数”打包入队，由单一后台任务串行执行
 * @note    用于把各模块的刷新动作与定时器/中断上下文解耦
 */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "cmsis_os.h"
#include "workqueue.h"

/* 队列消息：一次待执行的任务及其参数 */
typedef struct
{
	work_t work;    /* 待执行的任务函数（详见 app.c 的参数包装说明） */
	void* param;    /* 传给任务函数的参数 */
}work_msg_t;

/* 队列句柄 */
static QueueHandle_t workqueue_handle;

/* 工作队列任务：阻塞读队列，取出任务函数并执行 */
static void work_func(void* param)
{
	work_msg_t msg;
	
	while(1)
	{
		/* 阻塞等待下一条任务，取出后立即调用 */
		xQueueReceive(workqueue_handle,&msg,portMAX_DELAY);
		msg.work(msg.param);
	}
}

/* 初始化：创建工作队列并启动后台任务 */
void workqueue_init(void)
{
	/* 队列容量取 16：投递方以 portMAX_DELAY 阻塞等待，容量过小会增加阻塞风险 */
	workqueue_handle=xQueueCreate(16,sizeof(work_msg_t));
	configASSERT(workqueue_handle);
    xTaskCreate(work_func, "workqueue", 1024, NULL, osPriorityBelowNormal5, NULL);
}

/* 投递一个任务到队列，由后台任务异步执行 */
void workqueue_run(work_t work, void *param)
{
	configASSERT(workqueue_handle);
	work_msg_t msg={work,param};
	xQueueSend(workqueue_handle,&msg,portMAX_DELAY);
}
