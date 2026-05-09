#pragma once
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

// 0 = Auto, 1 = Manual
extern int brightness_mode;        // 当前模式
extern int current_brightness;     // 手动模式下的百分比 0~100
// 初始化（创建定时器；默认进入 Auto 模式）
void Brightness_Init(void);
// 立即按百分比 0~100 生效（用于手动调整预览/确认）
void apply_brightness(int percent);
// 切到自动亮度（开始定时采样 ADC 并更新）
void enable_auto_brightness(void);
// 保存并切到手动亮度（停止自动采样，固定在给定百分比）
void save_brightness(int percent);

#ifdef __cplusplus
}
#endif

