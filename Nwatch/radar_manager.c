#include "stm32f4xx.h"                  // Device header

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "radar_manager.h"
#include "driver_radar.h"
#include "cmsis_os.h"
#include "semphr.h"                        /* xSemaphoreGetMutexHolder 用 */


/*debug 变量*/
volatile uint32_t g_radarLoop = 0; //debug计数器
volatile eTaskState g_stDefault, g_stGame1, g_stPage, g_stMpu, g_stEnc, g_stInput;
volatile TaskHandle_t g_mutexHolder;
volatile char *g_holderName;
extern osThreadId  defaultTaskHandle;
extern TaskHandle_t game1TaskHandle;
extern SemaphoreHandle_t g_oledMutex;      /* main.c 里定义的那把锁 */

/*建立事件组来控制多任务系统*/
extern EventGroupHandle_t g_sysEvent;

void Radar_Task(void *param)
{
	
	while(1)
	{
		g_stDefault = eTaskGetState((TaskHandle_t)defaultTaskHandle);
		g_stGame1   = eTaskGetState(game1TaskHandle);

		g_mutexHolder = xSemaphoreGetMutexHolder(g_oledMutex);
		g_holderName  = (g_mutexHolder != NULL)
                    ? pcTaskGetName(g_mutexHolder)   /* ★ 谁拿着锁不放 */
                    : "(none)";
		
		g_radarLoop++;
		Radar_Process();
		if (Radar_IsPresent())
		{
			xEventGroupSetBits(g_sysEvent, EVT_PLAYER_PRESENT);
		}
		else
			xEventGroupClearBits(g_sysEvent, EVT_PLAYER_PRESENT);
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}
