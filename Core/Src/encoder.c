#include "encoder.h"
#include "cmsis_os2.h"
#include "ui_events.h"

// 简单去抖/限流（ms）
#define ENC_EDGE_DEBOUNCE_MS   2
#define BTN_DEBOUNCE_MS        10
#define POLL_INTERVAL_MS       2   // 轮询周期（越小越灵敏）
extern void Alarm_OnUserButton(void);


void EncoderTask(void *argument) {
    uint8_t last_clk = HAL_GPIO_ReadPin(ENC_CLK_Port, ENC_CLK_Pin);
    uint8_t last_sw  = HAL_GPIO_ReadPin(ENC_SW_Port,  ENC_SW_Pin);
    uint32_t last_clk_tick = HAL_GetTick();
    uint32_t last_sw_tick  = HAL_GetTick();

    for (;;) {
        // 读取当前值
        uint8_t clk = HAL_GPIO_ReadPin(ENC_CLK_Port, ENC_CLK_Pin);
        uint8_t dt  = HAL_GPIO_ReadPin(ENC_DT_Port,  ENC_DT_Pin);
        uint8_t sw  = HAL_GPIO_ReadPin(ENC_SW_Port,  ENC_SW_Pin);
        uint32_t now = HAL_GetTick();

        // —— 旋转：在 CLK 下降沿判断方向 ——
        if (last_clk == GPIO_PIN_SET && clk == GPIO_PIN_RESET) {
            if (now - last_clk_tick >= ENC_EDGE_DEBOUNCE_MS) {
                last_clk_tick = now;
                if (dt == GPIO_PIN_SET) UI_SendEvent(UI_EVENT_ENCODER_RIGHT);
                else                     UI_SendEvent(UI_EVENT_ENCODER_LEFT);
            }
        }
        last_clk = clk;

        // —— 按钮：低有效，带去抖 ——
        if (last_sw == GPIO_PIN_SET && sw == GPIO_PIN_RESET) {      // 按下沿
            if (now - last_sw_tick >= BTN_DEBOUNCE_MS) {
                last_sw_tick = now;
                // ★ 先尝试停止闹钟（若当前正在响）
				Alarm_OnUserButton();
				// 再把“按钮按下”发给 UI（用于正常菜单/暂停/返回等）
                UI_SendEvent(UI_EVENT_BUTTON_PRESS);
            }
        }
        last_sw = sw;

        osDelay(POLL_INTERVAL_MS);
    }
}
