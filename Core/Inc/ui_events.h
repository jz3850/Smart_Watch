#pragma once
#include "cmsis_os2.h"

typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_ENCODER_LEFT,     // 逆时针
    UI_EVENT_ENCODER_RIGHT,    // 顺时针
    UI_EVENT_BUTTON_PRESS      // 按下
} UI_EventType;

typedef struct {
    UI_EventType type;
} UI_Event;

// 事件队列句柄（全局）
extern osMessageQueueId_t uiEventQueueHandle;

// 发送事件（任务/中断均可用，timeout=0）
static inline void UI_SendEvent(UI_EventType type) {
    UI_Event evt = { .type = type };
    // 若在中断里调用也OK（CMSIS-RTOS v2 允许 timeout=0）
    osMessageQueuePut(uiEventQueueHandle, &evt, 0, 0);
}

