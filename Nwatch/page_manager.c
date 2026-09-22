#include "stm32f4xx.h"                  // Device header

#include "FreeRTOS.h"
#include "task.h"
#include "page_manager.h"
#include "driver_key.h"
#include "cmsis_os.h"
#include "stdbool.h"
#include "semphr.h"

extern osThreadId defaultTaskHandle;
extern TaskHandle_t game1TaskHandle;

static bool onGamePage = false;
volatile bool switchToGame = false;

static void StartPageSwitchTask(void *params)
{
	while(1)
	{
		if (Key_Read())
		{
			vTaskDelay(pdMS_TO_TICKS(20)); //消抖
			if (Key_Read())
			{
				if (onGamePage)
				{
					switchToGame = false;
					while ((eTaskGetState(game1TaskHandle) != eSuspended)) //等game1真的挂起
					{
						vTaskDelay(pdMS_TO_TICKS(5));
					}
					vTaskResume((TaskHandle_t)defaultTaskHandle);
				}
				else
				{
					switchToGame = true;
					while ((eTaskGetState(defaultTaskHandle) != eSuspended)) //等game1真的挂起
					{
						vTaskDelay(pdMS_TO_TICKS(5));
					}
					vTaskResume(game1TaskHandle);
				}
				onGamePage = !onGamePage;
				
				while(Key_Read());//等松手
			}
			
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void PageManager_Init(void)
{
	xTaskCreate(StartPageSwitchTask, "pageSwitch", 128, NULL,osPriorityNormal + 1, NULL);
}


