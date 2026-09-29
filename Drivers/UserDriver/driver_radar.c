#include "stm32f4xx.h"                  // Device header
#include "driver_radar.h"
#include "FreeRTOS.h"                   // ARM.FreeRTOS::RTOS:Core
#include "task.h"                       // ARM.FreeRTOS::RTOS:Core
#include <stdbool.h>
#include <string.h>

#define RADAR_RX_BUFSZ 256

/* 使用硒碘微手势识别雷达 */
#define RADAR_ABSENCE_MS   2000
#define XG_FRAME_LEN 6
#define XG_HEAD 0xAA
#define XG_TAIL 0x55
#define GATE 0x13

extern UART_HandleTypeDef huart2; 

static uint8_t s_rxBuf[RADAR_RX_BUFSZ + 1];
static uint8_t s_frameBuf[RADAR_RX_BUFSZ + 1];
static volatile uint16_t s_frameLen = 0;
static volatile uint8_t  s_frameReady = 0;
static volatile uint32_t s_lastSeenTick = 0;
static volatile uint8_t s_everSeen = 0;

void Radar_Init(void)
{
	s_frameReady = 0;
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, s_rxBuf, RADAR_RX_BUFSZ);
}

void Radar_Process(void)
{
	uint16_t i = 0;
	
	if (!s_frameReady) return;
	/* XenG101G 上报帧，固定 6 字节：
         [0]=0xAA  [1]=距离(0x00 表示无人)  [2]=挥手(Bit0)/次数(Bit4~7)
         [3]=预留  [4]=校验和([1]+[2]+[3]，超出取低字节)  [5]=0x55
         协议无固定对齐，需滑动查找帧头                                  */
	while (i + 6 <= s_frameLen)
	{
		if(s_frameBuf[i] == XG_HEAD && s_frameBuf[i + 5] == XG_TAIL
               && (uint8_t)(s_frameBuf[i + 1] + s_frameBuf[i + 2] + s_frameBuf[i + 3])
				== s_frameBuf[i + 4])
		{
			if (s_frameBuf[i + 1] > GATE)
			{
				s_lastSeenTick = xTaskGetTickCount();
				s_everSeen = 1;
			}
			i += XG_FRAME_LEN;
		}
		else
		{
			i++;
		}
	}
	__DMB();
	s_frameReady = 0;
}

bool Radar_IsPresent(void)
{
	if(!s_everSeen) return false;
	return (xTaskGetTickCount() - s_lastSeenTick) < pdMS_TO_TICKS(RADAR_ABSENCE_MS);
}

/*中断回调函数*/
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart->Instance != USART2) return;
	
	if (!s_frameReady && Size >= 6)
	{
		uint16_t len = (Size > RADAR_RX_BUFSZ) ? RADAR_RX_BUFSZ : Size;
		memcpy(s_frameBuf, s_rxBuf, len);
		s_frameBuf[len] = '\0';
        s_frameLen = len;
        __DMB();
        s_frameReady = 1;
	}
	
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, s_rxBuf, RADAR_RX_BUFSZ);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance != USART2) return;
	
	huart->ErrorCode = HAL_UART_ERROR_NONE;
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, s_rxBuf, RADAR_RX_BUFSZ);
}

