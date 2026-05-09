#ifndef __SSD1306_CONF_H__
#define __SSD1306_CONF_H__

// 选择接口方式（必须定义一个）
#define SSD1306_USE_I2C
// #define SSD1306_USE_SPI   // 如果用 SPI，就启用这一行而不是 I2C

// I2C 配置
#define SSD1306_I2C_PORT        hi2c1   // 使用的 I2C 句柄（来自 CubeMX）
#define SSD1306_I2C_ADDR        0x3C<<1    // OLED I2C 地址（0x3C 或 0x3D）

// 屏幕分辨率
#define SSD1306_WIDTH           128
#define SSD1306_HEIGHT          64

// 字体支持（启用/禁用节省 Flash）
#define SSD1306_INCLUDE_FONT_6x8    1
#define SSD1306_INCLUDE_FONT_7x10   1
#define SSD1306_INCLUDE_FONT_11x18  1
#define SSD1306_INCLUDE_FONT_16x26  1

#endif // __SSD1306_CONF_H__
