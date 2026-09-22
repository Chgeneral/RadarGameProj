// driver_key.h
#ifndef _DRIVER_ROTARY_ENCODER_H
#define _DRIVER_ROTARY_ENCODER_H
#include "stm32f4xx.h"                  // Device header

void RotaryEncoder_Init(void);
void RotaryEncoder_Read(int32_t *pCnt, int32_t *pSpeed);

#endif
