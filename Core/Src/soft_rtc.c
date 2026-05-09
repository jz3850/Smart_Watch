#include "stm32f4xx_hal.h"   // 按你的芯片改
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// 保存基准时间（校时刻）
static uint64_t base_ms = 0;
static int base_Y, base_M, base_D, base_h, base_m, base_s;

static int is_leap(int y){
    return ((y%4==0 && y%100!=0) || (y%400==0));
}
static int days_in_month(int y,int m){
    static const int dm[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2) return dm[1]+(is_leap(y)?1:0);
    return dm[m-1];
}

// soft_rtc.c
static uint8_t g_rtc_valid = 0;   // 0=未校时, 1=已校时

int SoftRTC_SetFromISO8601(const char *dt_in){
    if(!dt_in) return -1;
    char buf[48]={0};
    size_t n = strlen(dt_in);
    if (n >= sizeof(buf)) n = sizeof(buf)-1;
    memcpy(buf, dt_in, n);
    while (n>0 && (buf[n-1]=='\r' || buf[n-1]=='\n' || buf[n-1]==' ' || buf[n-1]=='\t')) buf[--n]=0;

    if (n < 19) return -2;
    if (buf[4]!='-' || buf[7]!='-' || (buf[10]!='T' && buf[10]!='t')) return -3;

    int Y,M,D,h,m,s;
    if (sscanf(buf, "%4d-%2d-%2dT%2d:%2d:%2d", &Y,&M,&D,&h,&m,&s) != 6) return -4;
    if (M<1||M>12 || D<1||D>31 || h<0||h>23 || m<0||m>59 || s<0||s>59) return -5;

    base_Y=Y; base_M=M; base_D=D; base_h=h; base_m=m; base_s=s;
    base_ms = HAL_GetTick();
    g_rtc_valid = 1;                 // 标记成功
    return 0;
}

uint8_t SoftRTC_IsValid(void){ return g_rtc_valid; }


// 每次循环调用，推进内部时钟
void SoftRTC_Tick(void){
    uint64_t now = HAL_GetTick();
    uint64_t diff_ms = now - base_ms;
    int add = (int)(diff_ms/1000ULL);
    if(add > 0){
        base_ms += (uint64_t)add * 1000ULL;
        base_s += add;
        // 进位
        while(base_s >= 60){ base_s-=60; base_m++; }
        while(base_m >= 60){ base_m-=60; base_h++; }
        while(base_h >= 24){ base_h-=24; base_D++;
            int dim = days_in_month(base_Y,base_M);
            if(base_D>dim){ base_D=1; base_M++; if(base_M>12){base_M=1;base_Y++;}}
        }
    }
}

void SoftRTC_Format(char *date, int dsz, char *time, int tsz){
    if(date && dsz>=11)
        snprintf(date, dsz, "%04d-%02d-%02d", base_Y, base_M, base_D);
    if(time && tsz>=9)
        snprintf(time, tsz, "%02d:%02d:%02d", base_h, base_m, base_s);
}

// 在文件尾部或合适位置加入 —— 强实现（会自动覆盖之前我给的 weak 版本）
bool rtc_get_time_hms(uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    // 先推进软时钟
    SoftRTC_Tick();

    // 还没校时的话返回 false
    if (!SoftRTC_IsValid()) return false;

    if (hh) *hh = (uint8_t)base_h;
    if (mm) *mm = (uint8_t)base_m;
    if (ss) *ss = (uint8_t)base_s;
    return true;
}

