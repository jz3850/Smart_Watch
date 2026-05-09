#ifndef BME280_MINI_H
#define BME280_MINI_H

#include "stm32f4xx_hal.h"   // 按你的芯片改
#include <stdint.h>

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t addr;     // 7-bit (0x76 or 0x77)
    // 温度补偿参数
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    // 湿度补偿参数
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3;
    int16_t  dig_H4; int16_t dig_H5; int8_t dig_H6;
    int32_t  t_fine;
} BME280;

HAL_StatusTypeDef BME280_Init(BME280 *dev, I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef BME280_ReadTempHum(BME280 *dev, float *temp_c, float *hum_pct);

#endif
