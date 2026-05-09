#include "buzzer.h"

void Buzzer_Init() {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); //// 启动 TIM2 的通道 1 PWM 输出
}

void Buzzer_SetFreq(uint32_t freq_hz) {
    uint32_t timer_clk = 1000000;   // 取决于你在 CubeMX 给 TIM2 的预分频设置
    uint32_t psc = 83;              // 示例：APB1=84MHz 时 PSC=83 → 1MHz
    __HAL_TIM_SET_PRESCALER(&htim2, psc);
    uint32_t arr = (timer_clk / freq_hz) - 1;
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (arr+1)/2);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
}

void Buzzer_BeepMs(uint32_t ms, uint32_t freq_hz) { // 时间，频率
    Buzzer_SetFreq(freq_hz);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_Delay(ms);
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
}
