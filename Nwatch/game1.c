/*
 * Project: N|Watch
 * Author: Zak Kemble, contact@zakkemble.co.uk
 * Copyright: (C) 2013 by Zak Kemble
 * License: GNU GPL v3 (see License.txt)
 * Web: http://blog.zakkemble.co.uk/diy-digital-wristwatch/
 */
#include <stdlib.h>
#include <stdio.h>

#include "cmsis_os.h"
#include "FreeRTOS.h"                   // ARM.FreeRTOS::RTOS:Core
#include "task.h"                       // ARM.FreeRTOS::RTOS:Core
#include "event_groups.h"               // ARM.FreeRTOS::RTOS:Event Groups
#include "semphr.h"                     // ARM.FreeRTOS::RTOS:Core

#include "draw.h"
#include "resources.h"

#include "driver_lcd.h"
#include "driver_mpu6050.h"
#include "driver_rotary_encoder.h"

#define NOINVERT	false
#define INVERT		true

#define sprintf_P  sprintf
#define PSTR(a)  a

#define PLATFORM_WIDTH	12
#define PLATFORM_HEIGHT	4
#define UPT_MOVE_NONE	0
#define UPT_MOVE_RIGHT	1
#define UPT_MOVE_LEFT	2
#define BLOCK_COLS		32
#define BLOCK_ROWS		5
#define BLOCK_COUNT		(BLOCK_COLS * BLOCK_ROWS)
#define MPU_NEUTRAL 90
#define MPU_DEADZONE 15

typedef struct{
	float x;
	float y;
	float velX;
	float velY;
}s_ball;

static const byte block[] ={
	0x07,0x07,0x07,
};

static const byte platform[] ={
	0x60,0x70,0x50,0x10,0x30,0xF0,0xF0,0x30,0x10,0x50,0x70,0x60,
};

static const byte ballImg[] ={
	0x03,0x03,
};

static const byte clearImg[] ={
	0,0,0,0,0,0,0,0,0,0,0,0,
};

static bool btnExit(void);
static bool btnRight(void);
static bool btnLeft(void);
void game1_draw(void);
static void game1_reset(void);

static byte uptMove;
static s_ball ball;
static bool* blocks;
static byte lives, lives_origin;
static uint score;
static byte platformX;

static uint32_t g_xres, g_yres, g_bpp;
static uint8_t *g_framebuffer;

extern volatile bool switchToGame;
static bool needFullDraw = true;
extern SemaphoreHandle_t g_oledMutex;

/*需求队列*/
QueueHandle_t xQueueEncoder;//旋转编码器队列
QueueHandle_t xQueueMpu; //MPU6050队列
QueueHandle_t xQueueSetInput; //队列集
QueueHandle_t xQueuePaddle; //挡球板仲裁队列

typedef struct  
{
	uint8_t move;
}input_data_t;

/*初始化队列任务*/
void Input_ObjectsCreate(void)
{
	xQueueEncoder = xQueueCreate(4, sizeof(input_data_t));
	xQueueMpu = xQueueCreate(4, sizeof(input_data_t));
	xQueuePaddle = xQueueCreate(4, sizeof(input_data_t));
	
	xQueueSetInput = xQueueCreateSet( 4 * 4 );
	xQueueAddToSet(xQueueEncoder, xQueueSetInput);
	xQueueAddToSet(xQueueMpu, xQueueSetInput);

}

/*采集层任务，用以接收传感器获得的信号*/

/* 旋转编码器任务 */
void EncoderTask(void *params)
{
	int32_t cnt, speed, last = 0;
	RotaryEncoder_Read(&cnt, &speed);
	last = cnt;
	while(1)
	{
		if (!switchToGame)/*不在游戏页，不读I2C*/
		{
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}
		RotaryEncoder_Read(&cnt, &speed);
		int32_t diff = cnt - last;
		last = cnt;
		if (diff != 0)
		{
			input_data_t d;
			d.move = (diff > 0) ? UPT_MOVE_RIGHT : UPT_MOVE_LEFT;
			xQueueSend(xQueueEncoder, &d, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

/* MPU6050任务 */
void MpuTask(void *params)
{
	int16_t AccX;
	struct mpu6050_data result;
	int ret;
	extern void WaitForI2C(void);
	extern void ReleaseI2C(void);
    while (1)
    {    
		if (!switchToGame)/*不在游戏页，不读I2C*/
		{
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}
		/* 读数据 */
		WaitForI2C();
		ret = MPU6050_ReadData(&AccX, NULL, NULL, NULL, NULL, NULL);
		ReleaseI2C();
		if (0 == ret)
		{
			/* 解析数据 */
			MPU6050_ParseData(AccX, 0, 0, 0, 0, 0, &result);
			input_data_t d;
			if (result.angle_x > MPU_NEUTRAL + MPU_DEADZONE)
				d.move = UPT_MOVE_RIGHT;          /* 方向正负上板再核对 */
			else if (result.angle_x < MPU_NEUTRAL - MPU_DEADZONE)
				d.move = UPT_MOVE_LEFT;
			else
				goto next;                         /* 在死区内，不发 */
			/* 写队列 */
			xQueueSend(xQueueMpu, &d, 0);
		}
		next:
			/* delay */
			vTaskDelay(20);
		}
}

/*输入任务，仲裁层*/
void InputTask(void *params)
{
	 QueueSetMemberHandle_t who;
    input_data_t d;
    while(1) {
        who = xQueueSelectFromSet(xQueueSetInput, portMAX_DELAY);
        /* 队列集只告诉你"谁有数据"，还得自己去那个队列 pop */
        if (who == xQueueEncoder)      xQueueReceive(xQueueEncoder, &d, 0);
        else if (who == xQueueMpu)     xQueueReceive(xQueueMpu,     &d, 0);
        else continue;

        xQueueSend(xQueuePaddle, &d, 0);   /* 统一命令给游戏 */
    }
}

/*游戏任务*/
void game1_task(void *params)
{		
    uint8_t dev, data, last_data;
    
    g_framebuffer = LCD_GetFrameBuffer(&g_xres, &g_yres, &g_bpp);
    draw_init();
    draw_end();
    
	uptMove = UPT_MOVE_NONE;

	ball.x = g_xres / 2;
	ball.y = g_yres - 10;
        
	ball.velX = -0.5;
	ball.velY = -0.6;
//	ball.velX = -1;
//	ball.velY = -1.1;

	blocks = pvPortMalloc(BLOCK_COUNT);// 只分配一次
    memset(blocks, 0, BLOCK_COUNT);
	
	lives = lives_origin = 3;
	score = 0;
	platformX = (g_xres / 2) - (PLATFORM_WIDTH / 2);

	/*创建队列，队列集*/
	Input_ObjectsCreate();
    /*创建任务*/
	xTaskCreate(EncoderTask, "EncoderTask", 128, NULL, osPriorityNormal + 2, NULL);
	xTaskCreate(MpuTask, "MpuTask", 128, NULL, osPriorityNormal + 2, NULL);
	xTaskCreate(InputTask, "InputTask", 128, NULL, osPriorityNormal + 2, NULL);

    while (1)
    {
		LCD_ClearFrameBuffer(); //清空帧缓存

		draw_end();

		game1_reset(); //重置游戏
		while (switchToGame) // 还在游戏页
		{
			game1_draw();
			vTaskDelay(50);
		}

		vTaskSuspend(NULL); //在任务内部进行挂起

    }
}

static bool btnExit()
{
	
	vPortFree(blocks);
	if(lives == 255)
	{
		//game1_start();
	}
	else
	{
		//pwrmgr_setState(PWR_ACTIVE_DISPLAY, PWR_STATE_NONE);	
		//animation_start(display_load, ANIM_MOVE_OFF);
		vTaskDelete(NULL);
	}
	return true;
}

static bool btnRight()
{
	uptMove = UPT_MOVE_RIGHT;
	return false;
}

static bool btnLeft()
{
	uptMove = UPT_MOVE_LEFT;
	return false;
}

void game1_draw()
{
	bool gameEnded = ((score >= BLOCK_COUNT) || (lives == 255));

	// Move ball
	// hide ball
	draw_bitmap(ball.x, ball.y, clearImg, 2, 2, NOINVERT, 0);
    draw_flushArea(ball.x, ball.y, 2, 8);

    // Draw platform
	input_data_t cmd;
	byte oldPlatformX = platformX;
	
	if(xQueueReceive(xQueuePaddle, &cmd, 0) == pdPASS)
	{
		if(cmd.move == UPT_MOVE_RIGHT)   platformX +=3;
		else if (cmd.move == UPT_MOVE_LEFT)  platformX -= 3;
		
		if (platformX > 250)  platformX = 0;               // 回绕保护
		else if (platformX > g_xres - PLATFORM_WIDTH) platformX = g_xres - PLATFORM_WIDTH;
	}
	// 擦掉旧位置
	draw_bitmap(oldPlatformX, g_yres - 8, clearImg, 12, 8, NOINVERT, 0);
    draw_flushArea(oldPlatformX, g_yres - 8, 12, 8);
	
	// 画新位置
	draw_bitmap(platformX, g_yres - 8, platform, 12, 8, NOINVERT, 0);
    draw_flushArea(platformX, g_yres - 8, 12, 8);
	
	if(!gameEnded)
	{
		ball.x += ball.velX;
		ball.y += ball.velY;
	}

	bool blockCollide = false;
	const float ballX = ball.x;
	const byte ballY = ball.y;

	// Block collision
	byte idx = 0;
	LOOP(BLOCK_COLS, x)
	{
		LOOP(BLOCK_ROWS, y)
		{
			if(!blocks[idx] && ballX >= x * 4 && ballX < (x * 4) + 4 && ballY >= (y * 4) + 8 && ballY < (y * 4) + 8 + 4)
			{
//				buzzer_buzz(100, TONE_2KHZ, VOL_UI, PRIO_UI, NULL);
				// led_flash(LED_GREEN, 50, 255); // 100ask todo
				blocks[idx] = true;

                // hide block
                draw_bitmap(x * 4, (y * 4) + 8, clearImg, 3, 8, NOINVERT, 0);                
                draw_flushArea(x * 4, (y * 4) + 8, 3, 8);                
				blockCollide = true;
				score++;
			}
			idx++;
		}
	}


	// Side wall collision
	if(ballX > g_xres - 2)
	{
		if(ballX > 240)
			ball.x = 0;		
		else
			ball.x = g_xres - 2;
		ball.velX = -ball.velX;		
	}
	if(ballX < 0)
  {
		ball.x = 0;		
		ball.velX = -ball.velX;	
  }

	// Platform collision
	bool platformCollision = false;
	if(!gameEnded && ballY >= g_yres - PLATFORM_HEIGHT - 2 && ballY < 240 && ballX >= platformX && ballX <= platformX + PLATFORM_WIDTH)
	{
		platformCollision = true;
		// buzzer_buzz(200, TONE_5KHZ, VOL_UI, PRIO_UI, NULL); // 100ask todo
		ball.y = g_yres - PLATFORM_HEIGHT - 2;
		if(ball.velY > 0)
			ball.velY = -ball.velY;
		ball.velX = ((float)rand() / (RAND_MAX / 2)) - 1; // -1.0 to 1.0
	}

	// Top/bottom wall collision
	if(!gameEnded && !platformCollision && (ballY > g_yres - 2 || blockCollide))
	{
		if(ballY > 240)
		{
			// buzzer_buzz(200, TONE_2_5KHZ, VOL_UI, PRIO_UI, NULL); // 100ask todo
			ball.y = 0;
		}
		else if(!blockCollide)
		{
			// buzzer_buzz(200, TONE_2KHZ, VOL_UI, PRIO_UI, NULL); // 100ask todo
			ball.y = g_yres - 1;
			lives--;
		}
		ball.velY *= -1;
	}

	// Draw ball
	draw_bitmap(ball.x, ball.y, ballImg, 2, 2, NOINVERT, 0);
    draw_flushArea(ball.x, ball.y, 2, 8);

    // Draw platform
    //draw_bitmap(platformX, g_yres - 8, platform, 12, 8, NOINVERT, 0);
    //draw_flushArea(platformX, g_yres - 8, 12, 8);

    if (needFullDraw)
    {
        needFullDraw = false;
        
    	// Draw blocks
    	idx = 0;
    	LOOP(BLOCK_COLS, x)
    	{
    		LOOP(BLOCK_ROWS, y)
    		{
    			if(!blocks[idx])
    			{
    				draw_bitmap(x * 4, (y * 4) + 8, block, 3, 8, NOINVERT, 0);
                    draw_flushArea(x * 4, (y * 4) + 8, 3, 8);                
    			}
    			idx++;
    		}
    	}
        
    }

	// Draw score
	char buff[6];
	sprintf_P(buff, PSTR("%u"), score);
	draw_string(buff, false, 0, 0);

    // Draw lives
    if(lives != 255)
    {
        LOOP(lives_origin, i)
        {
            if (i < lives)
                draw_bitmap((g_xres - (3*8)) + (8*i), 1, livesImg, 7, 8, NOINVERT, 0);
            else
                draw_bitmap((g_xres - (3*8)) + (8*i), 1, clearImg, 7, 8, NOINVERT, 0);
            draw_flushArea((g_xres - (3*8)) + (8*i), 1, 7, 8);    
        }
    }   

	// Got all blocks
	if(score >= BLOCK_COUNT)
		draw_string_P(PSTR(STR_WIN), false, 50, 32);

	// No lives left (255 because overflow)
	if(lives == 255)
		draw_string_P(PSTR(STR_GAMEOVER), false, 34, 32);

}

static void game1_reset(void)
{
	uptMove = UPT_MOVE_NONE;
	
	ball.x = g_xres / 2;
    ball.y = g_yres - 10;
    ball.velX = -0.5;
    ball.velY = -0.6;

    memset(blocks, 0, BLOCK_COUNT);   // 所有砖块恢复(未打掉)

    lives = lives_origin = 3;
    score = 0;
    platformX = (g_xres / 2) - (PLATFORM_WIDTH / 2);

    needFullDraw = true;              // 触发砖块整屏重绘
}
