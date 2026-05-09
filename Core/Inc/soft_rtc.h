#pragma once
#include <stdint.h>
#include <stdbool.h>
int  SoftRTC_SetFromISO8601(const char *dt_in);
uint8_t SoftRTC_IsValid(void);
void SoftRTC_Tick(void);
void SoftRTC_Format(char *date, int dsz, char *time, int tsz);
bool rtc_get_time_hms(uint8_t *hh, uint8_t *mm, uint8_t *ss);
