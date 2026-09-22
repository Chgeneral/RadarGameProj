#include "stm32f4xx.h"                  // Device header


#define KEY_GPIO_GROUP GPIOB
#define KEY_GPIO_PIN GPIO_PIN_5

int Key_Read(void)
{
	if (GPIO_PIN_RESET == HAL_GPIO_ReadPin(KEY_GPIO_GROUP, KEY_GPIO_PIN))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
