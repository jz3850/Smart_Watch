#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>   // size_t
#include <stdbool.h>  // bool
#include <assert.h>
#include <string.h>

#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"

#include "ldr.h"        // current_brightness / apply_brightness / enable_auto_brightness / save_brightness
#include "ui_task.h"
#include "ui_events.h"
#include "menu.h"       // menu_state / menu_index / show_menu()
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "soft_rtc.h"   // 软 RTC
#include "bme280.h"

extern I2C_HandleTypeDef hi2c1;
extern osMessageQueueId_t uiEventQueueHandle;

// 由 AlarmTask 提供的“按键停止”接口
extern void Alarm_OnUserButton(void);

// 亮度调节步进（百分比，0~100）
#define BRIGHTNESS_STEP   5

// ==============================================================================
//              Alarm 弹窗（覆盖层）
static volatile uint8_t g_alarm_popup_active = 0;
static uint8_t  g_alarm_popup_h=0, g_alarm_popup_m=0, g_alarm_popup_s=0;
static uint32_t g_alarm_popup_blink_ts = 0;
static uint8_t  g_alarm_popup_blink_on = 1;


// ==============================================================================
//                                   Time 页面
// ==============================================================================
static uint32_t time_last_draw = 0;
//  工具函数
static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static void ui_time_render(bool force) {
    // 未校时：提示等待（不每秒清屏抖动）
    if (!SoftRTC_IsValid()) {
        if (!force) return;
        ssd1306_Fill(Black);
        ssd1306_SetCursor(0,0);  ssd1306_WriteString("Time", Font_7x10, White);
        ssd1306_SetCursor(0,20); ssd1306_WriteString("waiting sync...", Font_6x8, White);
        ssd1306_SetCursor(0,40); ssd1306_WriteString("press to back",  Font_6x8, White);
        ssd1306_UpdateScreen();
        return;
    }

    char date[16], tim[16];
    SoftRTC_Format(date, sizeof(date), tim, sizeof(tim));

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("NY Local Time", Font_7x10, White);
    ssd1306_SetCursor(0,24); ssd1306_WriteString(date,           Font_6x8,  White);
    ssd1306_SetCursor(0,40); ssd1306_WriteString(tim,            Font_7x10, White);
    ssd1306_SetCursor(0,56); ssd1306_WriteString("press to back",Font_6x8,  White);
    ssd1306_UpdateScreen();
}

static void ui_time_on_enter(void) {
    menu_state = MENU_STATE_TIME;
    time_last_draw = 0;
    ui_time_render(true);   // 立即渲染一帧
}

static void ui_time_tick(void) {
    if (menu_state != MENU_STATE_TIME) return;
    SoftRTC_Tick(); // 推进软RTC
    if (g_alarm_popup_active) return;  // ★ 弹窗时不画屏
    uint32_t now = HAL_GetTick();
    if (now - time_last_draw >= 1000) {
        time_last_draw = now;
        ui_time_render(false);
    }
}

// ==============================================================================
//                                 Weather 页面
// ==============================================================================
static BME280 g_bme;
static uint8_t g_bme_ok = 0;
static uint32_t weather_last_draw = 0;

static void ui_weather_on_enter(void) {
    menu_state = MENU_STATE_WEATHER;
    weather_last_draw = 0;

    if (!g_bme_ok) {
        g_bme_ok = (BME280_Init(&g_bme, &hi2c1) == HAL_OK);
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("Weather", Font_7x10, White);
    ssd1306_SetCursor(0,56); ssd1306_WriteString("press to back", Font_6x8, White);
    ssd1306_UpdateScreen();
}

static void ui_weather_tick(void) {
    if (menu_state != MENU_STATE_WEATHER) return;
    if (g_alarm_popup_active) return;  // ★ 弹窗时不画屏
    uint32_t now = HAL_GetTick();
    if (now - weather_last_draw < 1000) return;   // 每秒刷一次
    weather_last_draw = now;

    // 读一次；失败尝试重 init 一次
    float tc=0, rh=0;
    HAL_StatusTypeDef st = HAL_ERROR;
    if (g_bme_ok) st = BME280_ReadTempHum(&g_bme, &tc, &rh);
    if (st != HAL_OK) {
        g_bme_ok = (BME280_Init(&g_bme, &hi2c1) == HAL_OK);
        if (g_bme_ok) st = BME280_ReadTempHum(&g_bme, &tc, &rh);
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("Weather", Font_7x10, White);

    if (st == HAL_OK) {
        // 手动格式化一位小数（避免 %f）
        int t_i = (int)tc, h_i = (int)rh;
        int t_d = (int)((tc - (float)t_i) * 10.0f); if (t_d<0) t_d=-t_d;
        int h_d = (int)((rh - (float)h_i) * 10.0f); if (h_d<0) h_d=-h_d;

        char line[24];
        ssd1306_SetCursor(0,24);
        snprintf(line,sizeof(line),"Temp: %d.%d C", t_i, t_d); ssd1306_WriteString(line, Font_6x8, White);

        ssd1306_SetCursor(0,36);
        snprintf(line,sizeof(line),"Humi: %d.%d %%", h_i, h_d); ssd1306_WriteString(line, Font_6x8, White);
    } else {
        ssd1306_SetCursor(0,24); ssd1306_WriteString("read FAIL", Font_6x8, White);
    }

    ssd1306_SetCursor(0,56); ssd1306_WriteString("press to back", Font_6x8, White);
    ssd1306_UpdateScreen();
}

// ==============================================================================
//                               Stopwatch 页面
// ==============================================================================
typedef enum {
    SW_STATE_IDLE = 0,     // 初始/清零
    SW_STATE_RUNNING,      // 计时中
    SW_STATE_PAUSED        // 暂停（显示暂停菜单）
} sw_state_t;

static sw_state_t sw_state = SW_STATE_IDLE;
static uint32_t   sw_last_draw_ms = 0;     // 上次渲染时间戳（用于限频）
static uint32_t   sw_start_ms = 0;         // 本次开始时的毫秒刻度
static uint32_t   sw_acc_ms = 0;           // 累积毫秒（暂停后保留）
static uint8_t    sw_menu_sel = 0;         // 暂停菜单：0=Continue, 1=End

// 计算当前总毫秒（acc + 运行增量）
static inline uint32_t sw_now_elapsed_ms(void) {
    if (sw_state == SW_STATE_RUNNING) {
        uint32_t now = HAL_GetTick();
        return sw_acc_ms + (now - sw_start_ms);
    }
    return sw_acc_ms;
}

// 把毫秒格式化为 "MM:SS:cc"（分:秒:百分秒）
static void sw_format_time(char *buf, size_t sz, uint32_t ms) {
    uint32_t total_cs = ms / 10;      // 百分秒
    uint32_t mm = (total_cs / 6000);  // 1分=6000百分秒
    uint32_t ss = (total_cs / 100) % 60;
    uint32_t cc = (total_cs % 100);
    snprintf(buf, sz, "%02lu:%02lu:%02lu", (unsigned long)mm, (unsigned long)ss, (unsigned long)cc);
}

static void ui_stopwatch_render(bool force_full)
{
    // 运行中 50ms 刷新一次，暂停/空闲或强制时立刻刷新
    uint32_t now = HAL_GetTick();
    if (!force_full && sw_state == SW_STATE_RUNNING) {
        if (now - sw_last_draw_ms < 50) return;
    }
    sw_last_draw_ms = now;

    char tbuf[16];
    sw_format_time(tbuf, sizeof(tbuf), sw_now_elapsed_ms());

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);   ssd1306_WriteString("Stopwatch", Font_7x10, White);
    ssd1306_SetCursor(0,26);  ssd1306_WriteString(tbuf,       Font_7x10, White);

    if (sw_state == SW_STATE_IDLE) {
        ssd1306_SetCursor(0,56); ssd1306_WriteString("press to start", Font_6x8, White);
    } else if (sw_state == SW_STATE_RUNNING) {
        ssd1306_SetCursor(0,56); ssd1306_WriteString("press to pause", Font_6x8, White);
    } else { // PAUSED：显示暂停菜单（置于底部区域）
        ssd1306_SetCursor(0,46); ssd1306_WriteString("PAUSED", Font_6x8, White);
        ssd1306_SetCursor(0,56);
        if (sw_menu_sel == 0) {
            ssd1306_WriteString("> Continue   End", Font_6x8, White);
        } else {
            ssd1306_WriteString("  Continue > End", Font_6x8, White);
        }
    }
    ssd1306_UpdateScreen();
}

static void ui_stopwatch_on_enter(void)
{
    menu_state = MENU_STATE_STOPWATCH;
    sw_state = SW_STATE_IDLE;
    sw_acc_ms = 0;
    sw_start_ms = 0;
    sw_menu_sel = 0;
    sw_last_draw_ms = 0;
    ui_stopwatch_render(true);
}

static void ui_stopwatch_tick(void)
{
    if (menu_state != MENU_STATE_STOPWATCH) return;
    if (g_alarm_popup_active) return;  // ★ 弹窗时不画屏
    ui_stopwatch_render(false);
}

// ==============================================================================
//                     Alarm: data model & storage (thread-safe)
// ==============================================================================
typedef struct {
    uint8_t hh, mm, ss;   // 24h
    uint8_t used;         // 0=empty, 1=valid
} alarm_t;

#define MAX_ALARMS      12
#define ALARM_LIST_PAGE 4   // 列表每页最多显示多少行

static alarm_t g_alarms[MAX_ALARMS];
static size_t  g_alarm_count = 0;
static osMutexId_t g_alarm_mutex;

static inline void alarm_lock(void){ if (g_alarm_mutex) osMutexAcquire(g_alarm_mutex, osWaitForever); }
static inline void alarm_unlock(void){ if (g_alarm_mutex) osMutexRelease(g_alarm_mutex); }

static size_t alarm_count(void) {
    size_t c; alarm_lock(); c = g_alarm_count; alarm_unlock(); return c;
}

static bool alarm_get(size_t idx, alarm_t *out) {
    bool ok = false; alarm_lock();
    if (idx < g_alarm_count && g_alarms[idx].used) { *out = g_alarms[idx]; ok = true; }
    alarm_unlock(); return ok;
}

static void alarm_compact_nolock(void) {
    size_t w = 0;
    for (size_t r = 0; r < MAX_ALARMS; ++r) {
        if (g_alarms[r].used) { if (w != r) g_alarms[w] = g_alarms[r]; w++; }
    }
    for (size_t i = w; i < MAX_ALARMS; ++i) g_alarms[i].used = 0;
    g_alarm_count = w;
}

// 导出给 AlarmTask 使用
bool alarm_delete_index(size_t idx) {
    bool ok = false; alarm_lock();
    if (idx < g_alarm_count && g_alarms[idx].used) { g_alarms[idx].used = 0; alarm_compact_nolock(); ok = true; }
    alarm_unlock(); return ok;
}

static bool alarm_add(uint8_t hh, uint8_t mm, uint8_t ss) {
    bool ok = false; alarm_lock();
    if (g_alarm_count < MAX_ALARMS) {
        g_alarms[g_alarm_count].hh = hh;
        g_alarms[g_alarm_count].mm = mm;
        g_alarms[g_alarm_count].ss = ss;
        g_alarms[g_alarm_count].used = 1;
        g_alarm_count++; ok = true;
    }
    alarm_unlock(); return ok;
}

// 提供给 AlarmTask 的“到点判断”
size_t alarm_find_due(uint8_t hh, uint8_t mm, uint8_t ss) {
    size_t idx = SIZE_MAX; alarm_lock();
    for (size_t i = 0; i < g_alarm_count; ++i) {
        if (!g_alarms[i].used) continue;
        if (g_alarms[i].hh == hh && g_alarms[i].mm == mm && g_alarms[i].ss == ss) { idx = i; break; }
    }
    alarm_unlock(); return idx;
}

// ==============================================================================
//                                 Alarm UI 页面
// ==============================================================================
typedef enum {
    ALM_STATE_MENU = 0,      // 闹钟子菜单（已设置/设置/返回主菜单）
    ALM_STATE_VIEW_LIST,     // 已设置闹钟的列表（可翻页/滑动）
    ALM_STATE_VIEW_ITEM_MENU,// 单项子菜单：删除 / 返回 / 回到闹钟子菜单
    ALM_STATE_SET_TIME,      // 设置闹钟：时->分->秒（当前字段闪烁）
    ALM_STATE_CREATED_TOAST  // 创建成功提示
} alm_state_t;

static alm_state_t alm_state = ALM_STATE_MENU;
static uint8_t alm_menu_idx = 0;          // 子菜单光标：0=已设置,1=设置,2=返回主菜单
static size_t  alm_list_cur = 0;          // 列表当前选中
static size_t  alm_list_top = 0;          // 列表窗口顶部索引
static uint8_t alm_item_menu_idx = 0;     // 单项子菜单：0=删除,1=返回(列表),2=回到闹钟子菜单

// 设置模式下的临时值与闪烁
static uint8_t alm_set_pos = 0;           // 0=HH, 1=MM, 2=SS
static uint8_t alm_set_hh = 0, alm_set_mm = 0, alm_set_ss = 0;
static uint32_t alm_blink_ts = 0;
static uint8_t  alm_blink_on = 1;
static uint32_t alm_created_ts = 0;

static void fmt_hhmmss(char *buf, size_t sz, uint8_t hh, uint8_t mm, uint8_t ss) {
    snprintf(buf, sz, "%02u:%02u:%02u", hh, mm, ss);
}

static void ui_alarm_render_menu(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("Alarm", Font_7x10, White);
    ssd1306_SetCursor(0,16); ssd1306_WriteString(alm_menu_idx==0?"> ":"  ", Font_6x8, White); ssd1306_WriteString("List alarms", Font_6x8, White);
    ssd1306_SetCursor(0,28); ssd1306_WriteString(alm_menu_idx==1?"> ":"  ", Font_6x8, White); ssd1306_WriteString("Set alarm",   Font_6x8, White);
    ssd1306_SetCursor(0,40); ssd1306_WriteString(alm_menu_idx==2?"> ":"  ", Font_6x8, White); ssd1306_WriteString("Back to main",Font_6x8, White);
    ssd1306_UpdateScreen();
}

static void ui_alarm_render_list(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0); ssd1306_WriteString("Alarms", Font_7x10, White);

    size_t cnt = alarm_count();
    if (cnt == 0) {
        ssd1306_SetCursor(0,28); ssd1306_WriteString("empty", Font_7x10, White);
        ssd1306_SetCursor(0,56); ssd1306_WriteString("press to back", Font_6x8, White);
        ssd1306_UpdateScreen();
        return;
    }

    // 窗口：从 alm_list_top 开始展示 ALARM_LIST_PAGE 行
    size_t end = alm_list_top + ALARM_LIST_PAGE;
    if (end > cnt) end = cnt;

    uint8_t y = 16;
    for (size_t i = alm_list_top; i < end; ++i) {
        alarm_t a; alarm_get(i, &a);
        char line[20]; fmt_hhmmss(line, sizeof(line), a.hh, a.mm, a.ss);

        ssd1306_SetCursor(0, y);
        ssd1306_WriteString(i == alm_list_cur ? "> " : "  ", Font_6x8, White);
        ssd1306_WriteString(line, Font_6x8, White);
        y += 12;
    }

    // 翻页提示
    if (cnt > ALARM_LIST_PAGE) {
        ssd1306_SetCursor(92,56);
        char pg[16];
        size_t page = alm_list_top/ALARM_LIST_PAGE + 1;
        size_t pages= (cnt + ALARM_LIST_PAGE - 1)/ALARM_LIST_PAGE;
        snprintf(pg, sizeof(pg), "%u/%u", (unsigned)page, (unsigned)pages);
        ssd1306_WriteString(pg, Font_6x8, White);
    }
    ssd1306_UpdateScreen();
}

static void ui_alarm_render_item_menu(void) {
    alarm_t a; alarm_get(alm_list_cur, &a);
    char timebuf[16]; fmt_hhmmss(timebuf, sizeof(timebuf), a.hh, a.mm, a.ss);

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString(timebuf, Font_7x10, White);
    ssd1306_SetCursor(0,26); ssd1306_WriteString(alm_item_menu_idx==0?"> ":"  ", Font_6x8, White); ssd1306_WriteString("Delete", Font_6x8, White);
    ssd1306_SetCursor(0,38); ssd1306_WriteString(alm_item_menu_idx==1?"> ":"  ", Font_6x8, White); ssd1306_WriteString("Back",   Font_6x8, White);
    ssd1306_SetCursor(0,50); ssd1306_WriteString(alm_item_menu_idx==2?"> ":"  ", Font_6x8, White); ssd1306_WriteString("Alarm menu", Font_6x8, White);
    ssd1306_UpdateScreen();
}

static void ui_alarm_render_set(void) {
    uint32_t now = HAL_GetTick();
    if (now - alm_blink_ts >= 400) { alm_blink_ts = now; alm_blink_on ^= 1; }

    char buf[12];
    uint8_t hh = alm_set_hh, mm = alm_set_mm, ss = alm_set_ss;

    char hh_s[3], mm_s[3], ss_s[3];
    snprintf(hh_s, 3, "%02u", hh);
    snprintf(mm_s, 3, "%02u", mm);
    snprintf(ss_s, 3, "%02u", ss);

    if (!alm_blink_on) {
        if (alm_set_pos == 0) { hh_s[0] = hh_s[1] = ' '; }
        else if (alm_set_pos == 1) { mm_s[0] = mm_s[1] = ' '; }
        else { ss_s[0] = ss_s[1] = ' '; }
    }
    snprintf(buf, sizeof(buf), "%s:%s:%s", hh_s, mm_s, ss_s);

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("Set alarm", Font_7x10, White);
    ssd1306_SetCursor(0,28); ssd1306_WriteString(buf,         Font_7x10, White);

    ssd1306_SetCursor(0,56);
    if (alm_set_pos < 2) ssd1306_WriteString("press: next",   Font_6x8, White);
    else                 ssd1306_WriteString("press: create", Font_6x8, White);

    ssd1306_UpdateScreen();
}

static void ui_alarm_render_created_toast(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,20); ssd1306_WriteString("Alarm created", Font_7x10, White);
    ssd1306_UpdateScreen();
}

static void ui_alarm_on_enter(void) {
    menu_state = MENU_STATE_ALARM;
    alm_state = ALM_STATE_MENU;
    alm_menu_idx = 0;
    alm_list_cur = 0; alm_list_top = 0;
    alm_item_menu_idx = 0;
    alm_set_pos = 0; alm_set_hh = 0; alm_set_mm = 0; alm_set_ss = 0;
    alm_blink_ts = HAL_GetTick(); alm_blink_on = 1;
    ui_alarm_render_menu();
}

static void ui_alarm_tick(void) {
    if (menu_state != MENU_STATE_ALARM) return;
    if (g_alarm_popup_active) return;  // ★ 弹窗时不画屏
    if (alm_state == ALM_STATE_SET_TIME) {
        ui_alarm_render_set();
    } else if (alm_state == ALM_STATE_CREATED_TOAST) {
        if (HAL_GetTick() - alm_created_ts >= 800) {
            alm_state = ALM_STATE_MENU;
            ui_alarm_render_menu();
        }
    }
}

// ==============================================================================
//                           Alarm 弹窗（覆盖层）
// ==============================================================================

static void ui_alarm_popup_render(void){
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", g_alarm_popup_h, g_alarm_popup_m, g_alarm_popup_s);

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);   ssd1306_WriteString("ALARM!",        Font_7x10, White);
    ssd1306_SetCursor(0, 24);  ssd1306_WriteString(tbuf,            Font_7x10, White);
    if (g_alarm_popup_blink_on){
        ssd1306_SetCursor(0, 52); ssd1306_WriteString("press knob to stop", Font_6x8, White);
    }
    ssd1306_UpdateScreen();
}

void UI_AlarmPopupBegin(uint8_t hh, uint8_t mm, uint8_t ss){
    g_alarm_popup_h = hh; g_alarm_popup_m = mm; g_alarm_popup_s = ss;
    g_alarm_popup_active = 1;
    g_alarm_popup_blink_ts = HAL_GetTick();
    g_alarm_popup_blink_on = 1;
}

void UI_AlarmPopupEnd(void){
    g_alarm_popup_active = 0;
    // 收起后重绘当前页面
    switch (menu_state){
        case MENU_STATE_MAIN:       show_menu(); break;
        case MENU_STATE_STOPWATCH:  ui_stopwatch_render(true); break;
        case MENU_STATE_ALARM:
            alm_state = ALM_STATE_MENU; ui_alarm_render_menu(); break;
        case MENU_STATE_TIME:       ui_time_render(true); break;
        case MENU_STATE_WEATHER:    ui_weather_on_enter(); break;
        default: show_menu(); break;
    }
}

static void ui_alarm_popup_tick(void){
    if (!g_alarm_popup_active) return;
    uint32_t now = HAL_GetTick();
    if (now - g_alarm_popup_blink_ts >= 400){
        g_alarm_popup_blink_ts = now;
        g_alarm_popup_blink_on ^= 1;
    }
    ui_alarm_popup_render();
}

// ==============================================================================
//                                 初始化
// ==============================================================================
void ui_init(void){
    static const osMutexAttr_t alarm_mutex_attr = { .attr_bits = osMutexPrioInherit };
    g_alarm_mutex = osMutexNew(&alarm_mutex_attr);
    assert(g_alarm_mutex != NULL);

    alarm_lock(); memset(g_alarms, 0, sizeof(g_alarms)); g_alarm_count = 0; alarm_unlock();
}

// ==============================================================================
//                                  UI 任务
// ==============================================================================
void UITask(void *argument) {
    ui_init();

    // 初始显示主菜单
    show_menu();
    UI_Event evt;

    for (;;) {
        // 周期刷新（无事件也运行）
        ui_time_tick();
        ui_weather_tick();
        ui_stopwatch_tick();
        ui_alarm_tick();
        ui_alarm_popup_tick();

        // 等待事件
        if (osMessageQueueGet(uiEventQueueHandle, &evt, NULL, 50) != osOK) {
            continue;
        }

        switch (evt.type) {
        case UI_EVENT_ENCODER_RIGHT:
        	if (g_alarm_popup_active) {
        	    // 闹钟弹窗期间忽略旋钮左右，避免其它页面抢画屏
        	    break;
        	} else if (menu_state == MENU_STATE_MAIN) {
                menu_index = (menu_index + 1) % MENU_COUNT; show_menu();
            } else if (menu_state == MENU_STATE_BRIGHTNESS) {
                menu_index = (menu_index + 1) % 2; show_menu();
            } else if (menu_state == MENU_STATE_BRIGHTNESS_MANUAL) {
                current_brightness = clamp_int(current_brightness + BRIGHTNESS_STEP, 0, 100);
                apply_brightness(current_brightness); show_menu();
            } else if (menu_state == MENU_STATE_STOPWATCH) {
                if (sw_state == SW_STATE_PAUSED) { sw_menu_sel = (sw_menu_sel + 1) & 0x01; ui_stopwatch_render(true); }
            } else if (menu_state == MENU_STATE_ALARM) {
                switch (alm_state) {
                    case ALM_STATE_MENU:
                        alm_menu_idx = (alm_menu_idx + 1) % 3; ui_alarm_render_menu(); break;
                    case ALM_STATE_VIEW_LIST: {
                        size_t cnt = alarm_count(); if (cnt == 0) break;
                        if (alm_list_cur + 1 < cnt) {
                            alm_list_cur++;
                            if (alm_list_cur >= alm_list_top + ALARM_LIST_PAGE) {
                                alm_list_top += ALARM_LIST_PAGE;
                                if (alm_list_top >= cnt) alm_list_top = (cnt/ALARM_LIST_PAGE)*ALARM_LIST_PAGE;
                            }
                            ui_alarm_render_list();
                        }
                        break; }
                    case ALM_STATE_VIEW_ITEM_MENU:
                        alm_item_menu_idx = (alm_item_menu_idx + 1) % 3; ui_alarm_render_item_menu(); break;
                    case ALM_STATE_SET_TIME:
                        if (alm_set_pos == 0) { alm_set_hh = (alm_set_hh + 1) % 24; }
                        else if (alm_set_pos == 1){ alm_set_mm = (alm_set_mm + 1) % 60; }
                        else                      { alm_set_ss = (alm_set_ss + 1) % 60; }
                        ui_alarm_render_set();
                        break;
                    default: break;
                }
            }
            break;

        case UI_EVENT_ENCODER_LEFT:
        	if (g_alarm_popup_active) {
        	    // 闹钟弹窗期间忽略旋钮左右，避免其它页面抢画屏
        	    break;
        	} else if (menu_state == MENU_STATE_MAIN) {
                menu_index = (menu_index == 0) ? (MENU_COUNT - 1) : (menu_index - 1); show_menu();
            } else if (menu_state == MENU_STATE_BRIGHTNESS) {
                menu_index = (menu_index == 0) ? 1 : (menu_index - 1); show_menu();
            } else if (menu_state == MENU_STATE_BRIGHTNESS_MANUAL) {
                current_brightness = clamp_int(current_brightness - BRIGHTNESS_STEP, 0, 100);
                apply_brightness(current_brightness); show_menu();
            } else if (menu_state == MENU_STATE_STOPWATCH) {
                if (sw_state == SW_STATE_PAUSED) { sw_menu_sel = (sw_menu_sel == 0) ? 1 : 0; ui_stopwatch_render(true); }
            } else if (menu_state == MENU_STATE_ALARM) {
                switch (alm_state) {
                    case ALM_STATE_MENU:
                        alm_menu_idx = (alm_menu_idx + 3 - 1) % 3; ui_alarm_render_menu(); break;
                    case ALM_STATE_VIEW_LIST: {
                        size_t cnt = alarm_count(); if (cnt == 0) break;
                        if (alm_list_cur > 0) {
                            alm_list_cur--;
                            if (alm_list_cur < alm_list_top) {
                                if (alm_list_top >= ALARM_LIST_PAGE) alm_list_top -= ALARM_LIST_PAGE; else alm_list_top = 0;
                            }
                            ui_alarm_render_list();
                        }
                        break; }
                    case ALM_STATE_VIEW_ITEM_MENU:
                        alm_item_menu_idx = (alm_item_menu_idx + 3 - 1) % 3; ui_alarm_render_item_menu(); break;
                    case ALM_STATE_SET_TIME:
                        if (alm_set_pos == 0) { alm_set_hh = (alm_set_hh + 24 - 1) % 24; }
                        else if (alm_set_pos == 1){ alm_set_mm = (alm_set_mm + 60 - 1) % 60; }
                        else                      { alm_set_ss = (alm_set_ss + 60 - 1) % 60; }
                        ui_alarm_render_set();
                        break;
                    default: break;
                }
            }
            break;

        case UI_EVENT_BUTTON_PRESS:
            if (g_alarm_popup_active){
                Alarm_OnUserButton();   // 通知 AlarmTask 立停
                UI_AlarmPopupEnd();     // 收起 UI 弹窗
                break;
            }

            if (menu_state == MENU_STATE_MAIN) {
                if (menu_index == 0) {          // Time
                    ui_time_on_enter();
                } else if (menu_index == 1) {   // Weather
                    ui_weather_on_enter();
                } else if (menu_index == 2) {   // Alarm（索引 3）
                	ui_alarm_on_enter();
                } else if (menu_index == 3) {   // Stopwatch（索引 2）
                    ui_stopwatch_on_enter();
                } else {                         // Brightness 菜单
                    menu_state = MENU_STATE_BRIGHTNESS;
                    menu_index = (brightness_mode == 0) ? 0 : 1;
                    show_menu();
                }
            }
            else if (menu_state == MENU_STATE_TIME) {
                menu_state = MENU_STATE_MAIN; menu_index = 0; show_menu();
            }
            else if (menu_state == MENU_STATE_WEATHER) {
                menu_state = MENU_STATE_MAIN; menu_index = 1; show_menu();
            }
            else if (menu_state == MENU_STATE_BRIGHTNESS) {
                if (menu_index == 0) { // Auto
                    enable_auto_brightness();
                    menu_state = MENU_STATE_MAIN; menu_index = 4; show_menu();
                } else { // Manual
                    menu_state = MENU_STATE_BRIGHTNESS_MANUAL; save_brightness(current_brightness); show_menu();
                }
            }
            else if (menu_state == MENU_STATE_BRIGHTNESS_MANUAL) {
                save_brightness(current_brightness);
                menu_state = MENU_STATE_MAIN; menu_index = 4; show_menu();
            }
            else if (menu_state == MENU_STATE_STOPWATCH) {
                if (sw_state == SW_STATE_IDLE) {
                    sw_acc_ms = 0; sw_start_ms = HAL_GetTick(); sw_state = SW_STATE_RUNNING; ui_stopwatch_render(true);
                } else if (sw_state == SW_STATE_RUNNING) {
                    uint32_t now = HAL_GetTick(); sw_acc_ms += (now - sw_start_ms);
                    sw_state = SW_STATE_PAUSED; sw_menu_sel = 0; ui_stopwatch_render(true);
                } else { // paused
                    if (sw_menu_sel == 0) { // Continue
                        sw_start_ms = HAL_GetTick(); sw_state = SW_STATE_RUNNING; ui_stopwatch_render(true);
                    } else { // End
                        sw_state = SW_STATE_IDLE; sw_acc_ms = 0; sw_start_ms = 0;
                        menu_state = MENU_STATE_MAIN; menu_index = 2; show_menu();
                    }
                }
            }
            else if (menu_state == MENU_STATE_ALARM) {
                switch (alm_state) {
                    case ALM_STATE_MENU:
                        if (alm_menu_idx == 0) {
                            alm_state = ALM_STATE_VIEW_LIST; alm_list_cur = 0; alm_list_top = 0; ui_alarm_render_list();
                        } else if (alm_menu_idx == 1) {
                            alm_state = ALM_STATE_SET_TIME; alm_set_pos = 0; alm_set_hh = alm_set_mm = alm_set_ss = 0;
                            alm_blink_ts = HAL_GetTick(); alm_blink_on = 1; ui_alarm_render_set();
                        } else {
                            menu_state = MENU_STATE_MAIN; menu_index = 3; show_menu();
                        }
                        break;
                    case ALM_STATE_VIEW_LIST: {
                        size_t cnt = alarm_count();
                        if (cnt == 0) { alm_state = ALM_STATE_MENU; ui_alarm_render_menu(); }
                        else { alm_item_menu_idx = 0; alm_state = ALM_STATE_VIEW_ITEM_MENU; ui_alarm_render_item_menu(); }
                        break; }
                    case ALM_STATE_VIEW_ITEM_MENU:
                        if (alm_item_menu_idx == 0) {
                            (void)alarm_delete_index(alm_list_cur);
                            size_t cnt = alarm_count();
                            if (cnt == 0) { alm_state = ALM_STATE_VIEW_LIST; alm_list_cur = 0; alm_list_top = 0; }
                            else if (alm_list_cur >= cnt) { alm_list_cur = cnt - 1; }
                            if (alm_list_cur < alm_list_top) { alm_list_top = (alm_list_cur / ALARM_LIST_PAGE) * ALARM_LIST_PAGE; }
                            alm_state = ALM_STATE_VIEW_LIST; ui_alarm_render_list();
                        } else if (alm_item_menu_idx == 1) {
                            alm_state = ALM_STATE_VIEW_LIST; ui_alarm_render_list();
                        } else {
                            alm_state = ALM_STATE_MENU; ui_alarm_render_menu();
                        }
                        break;
                    case ALM_STATE_SET_TIME:
                        if (alm_set_pos < 2) {
                            alm_set_pos++; alm_blink_ts = HAL_GetTick(); alm_blink_on = 1; ui_alarm_render_set();
                        } else {
                            if (alarm_add(alm_set_hh, alm_set_mm, alm_set_ss)) {
                                alm_state = ALM_STATE_CREATED_TOAST; alm_created_ts = HAL_GetTick(); ui_alarm_render_created_toast();
                            } else {
                                ssd1306_Fill(Black);
                                ssd1306_SetCursor(0,20); ssd1306_WriteString("List full", Font_7x10, White);
                                ssd1306_UpdateScreen(); osDelay(800);
                                alm_state = ALM_STATE_MENU; ui_alarm_render_menu();
                            }
                        }
                        break;
                    case ALM_STATE_CREATED_TOAST:
                        alm_state = ALM_STATE_MENU; ui_alarm_render_menu();
                        break;
                    default: break;
                }
            }
            break;

        default: break;
        }
    }
}
