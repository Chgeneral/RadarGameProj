#include "stm32f4xx.h"                  // Device header
#include "driver_rotary_encoder.h"

extern TIM_HandleTypeDef htim3;

/*编码器使用硬件正交编码*/
#define ENC_COUNTS_PER_STEP 4

static uint16_t s_last_cnt = 0; //上一次读到硬件计数器的原始值
static int32_t s_position = 0; //累计逻辑位置
static uint32_t s_last_tick = 0; //上次算速度的时间戳

/*旋转编码器初始化*/
void RotaryEncoder_Init(void)
{
	HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
	__HAL_TIM_SET_COUNTER(&htim3,0);
	s_last_cnt = 0;
	s_position = 0;
	s_last_tick = HAL_GetTick();
}

/* 读取：算增量方向速度 + 读按键
 *  *pCnt   累计位置（顺时针+，逆时针-）
 *  *pSpeed 本次相对上次的增量(带符号)，固定周期调用时正比于转速
 */
void RotaryEncoder_Read(int32_t *pCnt, int32_t *pSpeed)
{
	uint16_t now  = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
	
	/* 核心技巧：两个16位无符号相减后强转 int16_t，自动处理 65535→0 的回绕，
       无论正转反转、无论是否跨过 0，diff 都是正确的带符号增量 */
	int16_t diff = (int16_t)(now - s_last_cnt);
	s_last_cnt = now;
	s_position += diff;
	
	if(pCnt) *pCnt   = s_position / ENC_COUNTS_PER_STEP;  // 细分成“格”
	if (pSpeed) *pSpeed = diff;                              // 本周期增量(带方向)
}



