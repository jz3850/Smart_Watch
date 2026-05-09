#pragma once
#include "main.h"

// TIM2 是在 CubeMX 里生成的，在 tim.c 里定义了 htim2
extern TIM_HandleTypeDef htim2;
void Buzzer_Init();                     // 可选：做一次性启动
void Buzzer_SetFreq(uint32_t freq_hz);      // 设置频率
void Buzzer_BeepMs(uint32_t ms, uint32_t freq_hz);
