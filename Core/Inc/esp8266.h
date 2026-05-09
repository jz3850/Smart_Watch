#ifndef ESP8266_MIN_H
#define ESP8266_MIN_H

#include "stm32f4xx_hal.h"   // 按你的芯片改
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== 1) 联网 =====
// 传入 SSID、密码（开放网络传 NULL 或 ""）
// 可选 ip_out：若非 NULL，成功时写入 STA IP（如 "192.168.1.23"）
HAL_StatusTypeDef ESP_WiFiJoin(const char *ssid, const char *pass,
                               char *ip_out, size_t ip_out_sz);

// ===== 2) 获取时间 =====
// 从 worldtimeapi.org 取 America/New_York 的时间；成功时：
//  - 若 dt_out 非 NULL，写入 ISO8601（例如 2025-08-22T12:34:56-04:00）
//  - 自动调用 SoftRTC_SetFromISO8601(dt) 做软RTC校准
HAL_StatusTypeDef ESP_GetNYTime_TXT(char *dt_out, size_t dt_out_sz);

// ===== 3) 可保留的测试函数（网络体检） =====
// 在 OLED 上显示：是否已关联(AP)、是否有IP、是否能 ping 公网 IP、DNS 是否可用、TCP连通是否OK
void ESP_NetDiag_Show(void);

#ifdef __cplusplus
}
#endif
#endif
