#include <ldr.h>
#include "cmsis_os2.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdint.h>
#include <math.h>

/* ====== 你的工程里已经配置：PA4 -> ADC1_IN4 ====== */
extern ADC_HandleTypeDef hadc1;

/* ====== 全局状态 ====== */
int brightness_mode = 1;            // 0 Auto (默认), 1 Manual
int current_brightness = 100;        // 手动百分比 0~100，给个舒适默认

/* ====== 软件定时器：自动亮度每 100ms 更新一次 ====== */
#define BR_TIMER_PERIOD_MS   100
static osTimerId_t brTimer = NULL;

// ======= 调参开关（根据你的环境先粗调，再细调） =======
#define ADC_DARK        50      // 很暗时的ADC（先实测后填）
#define ADC_BRIGHT      2600    // 很亮时的ADC（先实测后填）
#define C_MIN           16      // 最低对比度(避免全黑看不清)
#define C_MAX           255     // 最高对比度(保留满幅)

#define GAMMA_POWER     0.55f   // γ<1 放大变化；0.45~0.7 常用
#define QUANT_STEP      32      // 量化步进(8/16/32)，越大越“跳得明显”
#define IIR_DEN         2       // 低通分母：2=50%新值，很灵敏；4=25%新值，较稳
// 方向：1=亮->更亮（默认），-1=反向（遮住更亮）
#define BRIGHT_DIR      1


/* === 小工具 === */
static inline uint8_t clamp_u8(int v, int lo, int hi) {
    if (v < lo) return (uint8_t)lo;
    if (v > hi) return (uint8_t)hi;
    return (uint8_t)v;
}
static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ====== 读一次 ADC & 简单平均 ====== */
static uint16_t ADC_ReadOnce(void) {
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return v;
}
static uint16_t ADC_ReadAvg(uint8_t n) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; ++i) sum += ADC_ReadOnce();
    return (uint16_t)(sum / n);
}

/* ====== 把“百分比 0~100”转换为 SSD1306 对比度 0~255 并下发 ====== */
static void set_contrast_percent(int percent) {
    // 线性映射到 0..255（如需更亮可提高下限或做 gamma）
    uint8_t c = (uint8_t)((percent * C_MAX) / 100);
    ssd1306_SetContrast(c);
}

/* ====== 手动模式 API ====== */
void apply_brightness(int percent) {
    current_brightness = clamp_int(percent, 0, 100);
    set_contrast_percent(current_brightness);
}

/* ====== 自动模式：定时器回调 ====== */
static void br_timer_cb() {

    // 1) 读ADC并做平均
    uint16_t adc = ADC_ReadAvg(8);

    // 2) 限定区间并归一化到[0,1]
    if (adc < ADC_DARK)   adc = ADC_DARK;
    if (adc > ADC_BRIGHT) adc = ADC_BRIGHT;
    float norm = (float)(adc - ADC_DARK) / (float)(ADC_BRIGHT - ADC_DARK);  // 0..1

    // 3) γ校正（γ<1 放大变化）
    float boosted = powf(norm, GAMMA_POWER);

    // 4) 可选反向（BRIGHT_DIR=-1 表示遮住更亮）
    if (BRIGHT_DIR < 0) boosted = 1.0f - boosted;

    // 5) 拉到对比度范围并量化
    int target = (int)(C_MIN + boosted * (C_MAX - C_MIN)); // C_MIN..C_MAX
    if (QUANT_STEP > 1) target = (target / QUANT_STEP) * QUANT_STEP;

    // 6) IIR 低通（IIR_DEN=2 很灵敏；=4 更稳）
    static int filt = C_MIN;
    filt = ( ((IIR_DEN - 1) * filt) + target ) / IIR_DEN;

    // 7) 下发
    ssd1306_SetContrast((uint8_t)filt);

    // （可选）调参时在底部打印当前ADC/对比度，确认是否明显
//    uint16_t raw = ADC_ReadAvg(8);
//    char line[24];
//    snprintf(line, sizeof(line), "ADC:%4u", raw);
//    ssd1306_FillRectangle(64, 54, 127, 63, Black);
//    ssd1306_SetCursor(64, 54);
//    ssd1306_WriteString(line, Font_6x8, White);
//    ssd1306_UpdateScreen();
}


/* ====== 模式切换 ====== */
void enable_auto_brightness(void) {
    brightness_mode = 0;
    if (brTimer == NULL) {
        const osTimerAttr_t attr = { .name = "brTimer" };
        brTimer = osTimerNew(br_timer_cb, osTimerPeriodic, NULL, &attr);
    }
    if (brTimer) osTimerStart(brTimer, BR_TIMER_PERIOD_MS);
}

void save_brightness(int percent) {
    brightness_mode = 1;
    if (brTimer) osTimerStop(brTimer);      // 停止自动更新
    apply_brightness(percent);              // 立刻按手动值生效
}

/* ====== 初始化：默认进入 Manual100 ====== */
void Brightness_Init(void) {
    set_contrast_percent(255);
    //enable_auto_brightness();  // 默认自适应
}



