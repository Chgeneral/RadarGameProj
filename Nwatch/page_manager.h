#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "stm32f4xx.h"                  // Device header
#include <stdlib.h>
#include <stdbool.h>


extern volatile bool switchToGame;

void PageManager_Init(void);   // 创建 game1Task（默认挂起）+ 按键切换任务

#endif
