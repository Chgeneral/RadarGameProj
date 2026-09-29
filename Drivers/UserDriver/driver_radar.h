#ifndef __DRIVER_RADAR_H
#define __DRIVER_RADAR_H

#include <stdbool.h>
#include <stdint.h>

void Radar_Init(void);        /* 启动 USART2 空闲+DMA 接收；须在调度器启动后、任务里调 */
bool Radar_IsPresent(void);   /* 当前是否有人（带超时抗抖） */
void Radar_Process(void);

#endif