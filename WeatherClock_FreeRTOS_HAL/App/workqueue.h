/**
 * @file    workqueue.h
 * @brief   轻量工作队列对外接口
 */
#ifndef __APP_WORKQUEUE_H__
#define __APP_WORKQUEUE_H__

/* 任务函数原型：统一携带一个 void* 参数，便于兼容有参/无参任务 */
typedef void (*work_t)(void *param);

void workqueue_init(void);                      /* 创建工作队列与后台任务 */
void workqueue_run(work_t work, void *param);   /* 投递任务到队列，异步执行 */

#endif /* __APP_WORKQUEUE_H__ */
