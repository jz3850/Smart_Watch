# STM32 + FreeRTOS 智能手表系统

基于 STM32F407G-DISC1 与 FreeRTOS 的嵌入式智能手表项目，集成 OLED 图形界面、多任务调度、WiFi 网络校时、闹钟管理、环境传感器与自动亮度调节等功能。

---

# 项目功能

- FreeRTOS 多任务系统
- SSD1306 OLED 图形界面
- KY-040 旋转编码器菜单控制
- ESP8266 网络时间同步
- 软件 RTC（SoftRTC）
- 多闹钟管理与提醒
- BME280 温湿度检测
- LDR 自动亮度调节
- 蜂鸣器提醒
- 模块化驱动设计

---

# 系统架构

<img width="1670" height="668" alt="7b1c29cc520d9075b25b1f9dce98439e" src="https://github.com/user-attachments/assets/8b8e352d-e09b-4ac8-8514-2708e8354fc9" />

系统以 STM32F407G-DISC1 为核心，通过 I2C、UART、ADC、GPIO、PWM 等接口连接多个外设模块。

| 模块 | 接口 | 功能 |
|---|---|---|
| SSD1306 OLED | I2C | UI 显示 |
| KY-040 编码器 | GPIO | 菜单控制 |
| ESP8266 ESP-01 | UART | WiFi / HTTP 网络校时 |
| BME280 | I2C | 温湿度检测 |
| LDR 光敏电阻 | ADC | 环境光检测 |
| 蜂鸣器 | PWM | 闹钟提醒 |

---

# FreeRTOS 任务架构

<img width="1916" height="1148" alt="d4b7335f0f54e66b9a67453aced5e76b" src="https://github.com/user-attachments/assets/772af63c-ef26-406e-a0f4-abb40bad61c5" />

系统采用事件驱动 + 多任务解耦设计：

| 任务 | 优先级 | 功能 |
|---|---|---|
| Encoder Task | High | 编码器输入采集与事件发送 |
| Alarm Task | Above Normal | 闹钟检测与弹窗控制 |
| UI Task | Normal | OLED UI 渲染与菜单逻辑 |

任务之间通过 Queue 与全局状态变量进行通信。

---

# 软件目录结构

```text
Core/
├── Inc/
│   ├── main.h
│   ├── cmsis_os.h
│   ├── ssd1306.h
│   ├── fonts.h
│   ├── ui.h
│   ├── alarm.h
│   └── soft_rtc.h
│
├── Src/
│   ├── main.c
│   ├── freertos.c
│   ├── ui.c
│   ├── alarm.c
│   ├── soft_rtc.c
│   ├── ssd1306.c
│   ├── fonts.c
│   └── esp8266.c
│
├── Drivers/
├── Middlewares/
│   └── FreeRTOS/
```

---

# UI 菜单结构

```text
主菜单
 ├── Time
 ├── Weather
 ├── Alarm
 │    ├── Add Alarm
 │    ├── Delete Alarm
 │    └── Alarm List
 ├── Stopwatch
 ├── Brightness
 │    ├── Auto
 │    └── Manual
 └── Contrast
```

---

# 闹钟工作流程

```text
RTC Tick
    ↓
Alarm Task 轮询
    ↓
检查闹钟列表
    ↓
触发闹钟
    ↓
蜂鸣器 + 弹窗提示
    ↓
用户按键确认
    ↓
删除闹钟 / 退出弹窗
```

---

# 网络校时流程

```text
STM32
  ↓ UART
ESP8266
  ↓ HTTP
worldtimeapi.org
  ↓
解析时间数据
  ↓
更新 SoftRTC
```

---

# 硬件平台

| 硬件 | 型号 |
|---|---|
| 主控 MCU | STM32F407G-DISC1 |
| OLED | SSD1306 128x64 |
| 编码器 | KY-040 |
| WiFi 模块 | ESP8266 ESP-01 |
| 温湿度传感器 | BME280 |
| 光敏传感器 | LDR |
| 蜂鸣器 | Passive Buzzer |

---

# 技术栈

- STM32CubeIDE
- STM32 HAL Driver
- FreeRTOS（CMSIS-RTOS v2）
- C 语言
- I2C / UART / ADC / PWM
- 事件驱动 UI 设计

---

# 核心设计思路

## 1. 事件驱动 UI

编码器任务仅负责采集输入，并通过 Queue 向 UI Task 发送事件，避免多个任务同时访问 OLED。

## 2. Software RTC

系统启动时通过 WiFi 获取网络时间，之后使用 `HAL_GetTick()` 驱动 SoftRTC 维持本地时间。

## 3. 自动亮度调节

支持：

- Auto Mode：根据环境光自动调整 OLED 亮度
- Manual Mode：用户手动设置亮度

## 4. 模块化设计

UI、Alarm、RTC、WiFi、Sensor 等模块独立封装，方便扩展与维护。

---

# 后续可扩展方向

- 天气 API 接入
- 蓝牙 BLE 支持
- 电池电量管理
- 低功耗模式
- 数据记录
- 更多 UI 动画效果

---

# 项目运行流程

```text
系统启动
   ↓
WiFi 网络校时
   ↓
OLED UI 初始化
   ↓
编码器菜单交互
   ↓
闹钟 / 秒表 / 亮度 / 天气功能
```

---

# 作者

Shuai Zou

Columbia University MSEE

方向：
Embedded Systems / Computer Architecture / System Software

---

# License

MIT License
