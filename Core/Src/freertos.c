/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
//#include "usart.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include "ui_events.h"
#include "encoder.h"
#include "ui_task.h"
#include "soft_rtc.h"
#include "buzzer.h"
#include "esp8266.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

// 声明队列
osMessageQueueId_t uiEventQueueHandle;

#define ALARM_FLAG_STOP   (1U << 0)
static volatile uint8_t s_ringing = 0;
static osThreadId_t gAlarmThreadId = NULL;
extern void UI_AlarmPopupBegin(uint8_t hh, uint8_t mm, uint8_t ss);
extern void UI_AlarmPopupEnd(void);

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for myTask02 */
osThreadId_t myTask02Handle;
const osThreadAttr_t myTask02_attributes = {
  .name = "myTask02",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for myTask03 */
osThreadId_t myTask03Handle;
const osThreadAttr_t myTask03_attributes = {
  .name = "myTask03",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for myTask04 */
osThreadId_t myTask04Handle;
const osThreadAttr_t myTask04_attributes = {
  .name = "myTask04",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void AlarmTask(void *argument);
void UITask(void *argument);
void EncoderTask(void *argument);
void Alarm_OnUserButton(void);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
	uiEventQueueHandle = osMessageQueueNew(16, sizeof(UI_Event), NULL);

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */

  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of myTask02 */
  myTask02Handle = osThreadNew(AlarmTask, NULL, &myTask02_attributes);

  /* creation of myTask03 */
  myTask03Handle = osThreadNew(UITask, NULL, &myTask03_attributes);

  /* creation of myTask04 */
  myTask04Handle = osThreadNew(EncoderTask, NULL, &myTask04_attributes);

  /* USER CODE BEGIN RTOS_THREADS */

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
	  osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_AlarmTask */
/**
* @brief Function implementing the myTask02 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_AlarmTask */
void AlarmTask(void *argument)
{
	gAlarmThreadId = osThreadGetId();

	for(;;){
		osDelay(200);  // 200ms 轮询当前时间

		uint8_t hh, mm, ss;
		if (!rtc_get_time_hms(&hh,&mm,&ss)) continue;

		size_t idx = alarm_find_due(hh, mm, ss);
		if (idx != SIZE_MAX){
			s_ringing = 1;

			// ★ 弹出 UI 提示页（显示 HH:MM:SS）
			UI_AlarmPopupBegin(hh, mm, ss);

			for (int i = 0; i < 15; ++i){
				// 立即查看是否被打断（0 超时且会清除此标志）
				uint32_t f = osThreadFlagsWait(ALARM_FLAG_STOP, osFlagsWaitAny, 0);//等待标志，等待条件，超时时间
				if ((int32_t)f >= 0) break;

				Buzzer_BeepMs(300, 2000);  // 响 300ms @2kHz
				// 静音 200ms，可被打断
				f = osThreadFlagsWait(ALARM_FLAG_STOP, osFlagsWaitAny, 200);
				if ((int32_t)f >= 0) break;
			}

			(void)alarm_delete_index(idx);
			s_ringing = 0;

			// ★ 收起 UI 提示页
			UI_AlarmPopupEnd();
		}
	}
}

/* USER CODE BEGIN Header_UITask */
/**
* @brief Function implementing the myTask03 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_UITask */


/* USER CODE BEGIN Header_EncoderTask */
/**
* @brief Function implementing the myTask04 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_EncoderTask */


/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void Alarm_OnUserButton(void){
    if (s_ringing && gAlarmThreadId){
        osThreadFlagsSet(gAlarmThreadId, ALARM_FLAG_STOP);
    }
}
/* USER CODE END Application */

