#ifndef MENU_H
#define MENU_H

#include "ssd1306.h"
#include "ssd1306_fonts.h"

// 菜单数量（主菜单）
#define MENU_COUNT 5

typedef enum {
    MENU_STATE_MAIN,              // 主菜单
    MENU_STATE_BRIGHTNESS,        // 亮度模式选择（自动/手动）
    MENU_STATE_BRIGHTNESS_MANUAL, // 手动调节
    MENU_STATE_TIME,              // << 新增：Time 实时显示页
	MENU_STATE_WEATHER,
	MENU_STATE_STOPWATCH,
	MENU_STATE_ALARM
} MenuState;


extern const char* menu_items[MENU_COUNT];
extern volatile int menu_index;
extern volatile MenuState menu_state;

void show_menu(void);

#endif
