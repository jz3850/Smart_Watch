#include "bme280.h"
#include <string.h>

static HAL_StatusTypeDef rd(I2C_HandleTypeDef *hi2c, uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len){
    return HAL_I2C_Mem_Read(hi2c, (addr<<1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}
static HAL_StatusTypeDef wr(I2C_HandleTypeDef *hi2c, uint8_t addr, uint8_t reg, uint8_t val){
    return HAL_I2C_Mem_Write(hi2c, (addr<<1), reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
}

static uint16_t U16(const uint8_t *p){ return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static int16_t  S16(const uint8_t *p){ return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8)); }

HAL_StatusTypeDef BME280_Init(BME280 *dev, I2C_HandleTypeDef *hi2c){
    memset(dev, 0, sizeof(*dev));
    dev->hi2c = hi2c;

    // 探测 0x76 / 0x77
    uint8_t id=0;
    dev->addr = 0x76;
    if (rd(hi2c, dev->addr, 0xD0, &id, 1) != HAL_OK || id != 0x60){
        dev->addr = 0x77;
        if (rd(hi2c, dev->addr, 0xD0, &id, 1) != HAL_OK || id != 0x60) return HAL_ERROR;
    }

    // 软复位
    wr(hi2c, dev->addr, 0xE0, 0xB6);
    HAL_Delay(2);

    // 读取补偿系数
    uint8_t buf[26];
    if (rd(hi2c, dev->addr, 0x88, buf, 26) != HAL_OK) return HAL_ERROR;
    dev->dig_T1 = U16(&buf[0]);
    dev->dig_T2 = S16(&buf[2]);
    dev->dig_T3 = S16(&buf[4]);

    // 湿度系数分散在 0xA1、0xE1..0xE7
    if (rd(hi2c, dev->addr, 0xA1, &dev->dig_H1, 1) != HAL_OK) return HAL_ERROR;
    uint8_t hb[7];
    if (rd(hi2c, dev->addr, 0xE1, hb, 7) != HAL_OK) return HAL_ERROR;
    dev->dig_H2 = (int16_t)((hb[1]<<8) | hb[0]);
    dev->dig_H3 = hb[2];
    dev->dig_H4 = (int16_t)((hb[3]<<4) | (hb[4] & 0x0F));
    dev->dig_H5 = (int16_t)((hb[5]<<4) | (hb[4] >> 4));
    dev->dig_H6 = (int8_t)hb[6];

    // 配置：湿度 oversampling x1（0xF2），温度 oversampling x1，模式先睡眠（0xF4）
    wr(hi2c, dev->addr, 0xF2, 0x01);
    wr(hi2c, dev->addr, 0xF5, 0x00); // config 默认
    wr(hi2c, dev->addr, 0xF4, 0x20); // osrs_t=x1 (001), osrs_p=0, mode=sleep(00) -> 0b001_000_00 = 0x20
    return HAL_OK;
}

HAL_StatusTypeDef BME280_ReadTempHum(BME280 *dev, float *temp_c, float *hum_pct){
    // 触发一次 Forced 测量：osrs_t=x1, osrs_p=0, mode=01
    if (wr(dev->hi2c, dev->addr, 0xF4, 0x21) != HAL_OK) return HAL_ERROR;

    // 等待测量完成：状态寄存器 0xF3 的 bit3 measuring=0
    uint8_t stat=0; uint32_t start = HAL_GetTick();
    do {
        if (rd(dev->hi2c, dev->addr, 0xF3, &stat, 1) != HAL_OK) return HAL_ERROR;
        if (!(stat & 0x08)) break;
    } while (HAL_GetTick() - start < 20); // x1 很快

    // 读取原始数据（温度 20bit，湿度 16bit）
    uint8_t raw[8];
    if (rd(dev->hi2c, dev->addr, 0xF7, raw, 8) != HAL_OK) return HAL_ERROR;
    int32_t adc_T = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)raw[6] << 8)  | raw[7]);

    // 温度补偿（Bosch 公式）
    int32_t var1 = ((((adc_T>>3) - ((int32_t)dev->dig_T1<<1))) * ((int32_t)dev->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T>>4) - ((int32_t)dev->dig_T1)) * ((adc_T>>4) - ((int32_t)dev->dig_T1))) >> 12) *
                    ((int32_t)dev->dig_T3)) >> 14;
    dev->t_fine = var1 + var2;
    int32_t T = (dev->t_fine * 5 + 128) >> 8;  // 0.01°C
    if (temp_c) *temp_c = T / 100.0f;

    // 湿度补偿（Bosch 公式）
    int32_t v_x1_u32r;
	v_x1_u32r = (dev->t_fine - ((int32_t)76800));
	v_x1_u32r = (((((adc_H << 14) - (((int32_t)dev->dig_H4) << 20) - (((int32_t)dev->dig_H5) * v_x1_u32r)) + 16384) >> 15) *
				  (((((((v_x1_u32r * ((int32_t)dev->dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)dev->dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
				  ((int32_t)dev->dig_H2) + 8192) >> 14));
	v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)dev->dig_H1)) >> 4));
	v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
	v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
	float H = (v_x1_u32r >> 12) / 1024.0f; // %RH
	if (hum_pct) *hum_pct = H;

    return HAL_OK;
}
