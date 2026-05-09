#pragma once
#include "main.h"
#include <stdint.h>

// === 根据你的接线修改这里 ===
#define ENC_CLK_Port   GPIOA
#define ENC_CLK_Pin    GPIO_PIN_0
#define ENC_DT_Port    GPIOA
#define ENC_DT_Pin     GPIO_PIN_1
#define ENC_SW_Port    GPIOA
#define ENC_SW_Pin     GPIO_PIN_9

void EncoderTask(void *argument);   // 旋转编码器任务
