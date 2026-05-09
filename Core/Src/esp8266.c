#include "esp8266.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

#include "ssd1306.h"        // 用于体检/提示（如不想依赖OLED，可删显示语句）
#include "ssd1306_fonts.h"
#include "soft_rtc.h"       // SoftRTC_SetFromISO8601

#include "core_cm4.h"       // F1=CM3；若是 F4 请改为 core_cm4.h

// ===== 内部小工具 =====
static void DWT_Init(void) {
    // 使能DWT计数器（一次）
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static HAL_StatusTypeDef UART2_SendStr(const char *s) {
    return HAL_UART_Transmit(&huart2, (uint8_t*)s, (uint16_t)strlen(s), 1000);
}

// 轮询只读，直到超时或缓冲满；避免 HAL_Delay
static HAL_StatusTypeDef UART2_Recv(uint8_t *buf, uint16_t maxlen,
                                    uint32_t timeout_ms, uint16_t *out_len) {
    uint32_t cycles_per_ms = SystemCoreClock / 1000U;
    uint32_t deadline = DWT->CYCCNT + cycles_per_ms * timeout_ms;

    uint16_t n = 0; uint8_t ch;
    while ((int32_t)(DWT->CYCCNT - deadline) < 0 && n < maxlen) {
        if (HAL_UART_Receive(&huart2, &ch, 1, 0) == HAL_OK) {
            buf[n++] = ch;
        }
    }
    if (out_len) *out_len = n;
    return (n > 0) ? HAL_OK : HAL_TIMEOUT;
}

// 发送 AT 并等待期望子串（非阻塞轮询）
static HAL_StatusTypeDef ESP_AT_WaitEither(const char *cmd,
                                           const char *expect1,
                                           const char *expect2,
                                           char *echo, uint16_t echo_sz) {
    // 清残留
    uint8_t ch;
    while (HAL_UART_Receive(&huart2, &ch, 1, 0) == HAL_OK) {}

    if (cmd && cmd[0]) {
        if (UART2_SendStr(cmd) != HAL_OK) return HAL_ERROR;
        if (UART2_SendStr("\r\n") != HAL_OK) return HAL_ERROR;
    }

    const uint32_t MAX_SPINS = 1500000u;
    uint8_t buf[256]; uint16_t n = 0;
    for (uint32_t spins = 0; spins < MAX_SPINS && n < sizeof(buf)-1; ++spins) {
        if (HAL_UART_Receive(&huart2, &ch, 1, 0) == HAL_OK) {
            buf[n++] = ch; buf[n] = 0;
            if ((expect1 && strstr((char*)buf, expect1)) ||
                (expect2 && strstr((char*)buf, expect2))) {
                if (echo && echo_sz) {
                    uint16_t c = (n<echo_sz-1)?n:(echo_sz-1);
                    memcpy(echo, buf, c); echo[c]=0;
                }
                return HAL_OK;
            }
        }
    }
    if (echo && echo_sz) {
        uint16_t c = (n<echo_sz-1)?n:(echo_sz-1);
        memcpy(echo, buf, c); echo[c]=0;
    }
    return HAL_TIMEOUT;
}

// 拉长等待窗口（适合 CWJAP 等异步过程）
static HAL_StatusTypeDef ESP_AT_SendWait_Long(const char *cmd, const char *expect,
                                              uint32_t repeats, char *echo, uint16_t echo_sz) {
    char tmp[160]={0};
    HAL_StatusTypeDef st = ESP_AT_WaitEither(cmd, expect, "OK", tmp, sizeof(tmp));
    if (echo && echo_sz) strncat(echo, tmp, echo_sz-1);
    for (uint32_t i=1; i<repeats && st!=HAL_OK; ++i) {
        tmp[0]=0;
        st = ESP_AT_WaitEither("", expect, "OK", tmp, sizeof(tmp));
        strncat(echo, tmp, echo_sz-1);
    }
    return st;
}

// ===== 1) 联网（支持开放网 / 有密码） =====
HAL_StatusTypeDef ESP_WiFiJoin(const char *ssid, const char *pass,
                               char *ip_out, size_t ip_out_sz)
{
    if (!ssid) return HAL_ERROR;
    DWT_Init();

    char echo[640]={0}, cmd[160];

    // STA 模式（容忍 no change）
    (void)ESP_AT_WaitEither("AT+CWMODE_CUR=1", "OK", "no change", echo, sizeof(echo));

    // 断开旧连接（忽略错误）
    (void)ESP_AT_WaitEither("AT+CWQAP", "OK", "ERROR", echo, sizeof(echo));

    // 连接
    if (pass && pass[0]) {
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"\"", ssid);  // 开放网
    }

    // CWJAP 需要较长时间：累计读取“WIFI CONNECTED / WIFI GOT IP / OK”
    echo[0]=0;
    HAL_StatusTypeDef ok = ESP_AT_SendWait_Long(cmd, "WIFI GOT IP", 16, echo, sizeof(echo));
    if (ok != HAL_OK) {
        if (strstr(echo, "OK")) ok = HAL_OK;
    }
    if (ok != HAL_OK) {
        // OLED 简提示（可删）
        ssd1306_Fill(Black);
        ssd1306_SetCursor(0,0); ssd1306_WriteString("JOIN:FAIL", Font_6x8, White);
        ssd1306_UpdateScreen();
        return HAL_TIMEOUT;
    }

    // 查询 IP
    echo[0]=0;
    ok = ESP_AT_WaitEither("AT+CIFSR", "OK", NULL, echo, sizeof(echo));
    if (ok == HAL_OK && ip_out && ip_out_sz > 0) {
        const char *p = strstr(echo, "STAIP");
        ip_out[0] = 0;
        if (p) {
            const char *q=strchr(p,'"'); const char *r=q?strchr(q+1,'"'):NULL;
            if (q&&r && r-q-1 < (int)ip_out_sz) { memcpy(ip_out,q+1,r-q-1); ip_out[r-q-1]=0; }
        }
    }

    // OLED 简提示（可删）
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0); ssd1306_WriteString("WiFi OK", Font_6x8, White);
    ssd1306_UpdateScreen();

    return HAL_OK;
}

// ===== 2) 获取纽约当地时间（TXT接口）并校准软RTC =====
HAL_StatusTypeDef ESP_GetNYTime_TXT(char *dt_out, size_t dt_out_sz)
{
    const char *host = "worldtimeapi.org";
    const char *path = "/api/timezone/America/New_York.txt";

    char echo[256]={0}, cmd[160], req[256];

    // 单连接、非透传
    (void)ESP_AT_WaitEither("AT+CIPMODE=0", "OK", "ERROR", echo, sizeof(echo));
    (void)ESP_AT_WaitEither("AT+CIPMUX=0",  "OK", "ERROR", echo, sizeof(echo));

    // TCP:80
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",80", host);
    if (ESP_AT_WaitEither(cmd, "CONNECT", "ALREADY CONNECT", echo, sizeof(echo)) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    // GET / HTTP/1.0
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: esp8266\r\n\r\n",
             path, host);

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)strlen(req));
    if (ESP_AT_WaitEither(cmd, ">", NULL, echo, sizeof(echo)) != HAL_OK) {
        (void)ESP_AT_WaitEither("AT+CIPCLOSE", "OK", "ERROR", echo, sizeof(echo));
        return HAL_TIMEOUT;
    }
    (void)ESP_AT_WaitEither(req, "SEND OK", NULL, echo, sizeof(echo));

    // 读取直到出现 header/body 分隔
    char resp[3000]={0};
    int have_ipd = 0;
    for (int i=0;i<120;i++){
        char tmp[200]={0};
        (void)ESP_AT_WaitEither("", "OK", NULL, tmp, sizeof(tmp));
        strncat(resp, tmp, sizeof(resp)-1);
        if (!have_ipd && strstr(resp, "+IPD")) have_ipd = 1;
        if (have_ipd && strstr(resp, "\r\n\r\n")) break;
    }
    (void)ESP_AT_WaitEither("AT+CIPCLOSE", "OK", "ERROR", echo, sizeof(echo));

    if (!have_ipd) return HAL_TIMEOUT;

    // 找正文起点（最后一个 \r\n\r\n 之后）
    const char *sep=NULL, *scan=resp;
    while (1){ const char *nxt=strstr(scan,"\r\n\r\n"); if(!nxt) break; sep=nxt; scan=nxt+4; }
    if (!sep) return HAL_TIMEOUT;
    const char *body = sep + 4;

    // 找 "datetime:" 行并取一整个 token
    const char *line = strstr(body, "datetime:");
    if (!line) line = strstr(resp, "datetime:");
    if (!line) return HAL_TIMEOUT;

    line += 9; while (*line==' '||*line=='\t') line++;
    char dt[48]={0};
    sscanf(line, "%47s", dt);
    int len = (int)strlen(dt);
    while (len>0 && (dt[len-1]=='\r'||dt[len-1]=='\n'||dt[len-1]==' '||dt[len-1]=='\t')) dt[--len]=0;
    if (len < 19) return HAL_ERROR; // 保护

    // 校准软RTC
    (void)SoftRTC_SetFromISO8601(dt);

    // 若调用者要拿原始 ISO8601
    if (dt_out && dt_out_sz>0) {
        size_t n = strlen(dt); if (n >= dt_out_sz) n = dt_out_sz-1;
        memcpy(dt_out, dt, n); dt_out[n]=0;
    }
    return HAL_OK;
}

// ===== 3) 测试函数：网络体检（保留） =====
void ESP_NetDiag_Show(void) {
    char buf[1024]={0};

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0,0);  ssd1306_WriteString("NetDiag...", Font_6x8, White);

    // JAP?
    (void)ESP_AT_WaitEither("AT+CWJAP?", "OK", NULL, buf, sizeof(buf));
    ssd1306_SetCursor(0,10);
    ssd1306_WriteString(strstr(buf,"+CWJAP:")?"JAP:OK":"JAP:FAIL", Font_6x8, White);

    // IP?
    buf[0]=0; (void)ESP_AT_WaitEither("AT+CIFSR", "OK", NULL, buf, sizeof(buf));
    ssd1306_SetCursor(64,10);
    ssd1306_WriteString(strstr(buf,"STAIP,\"")?"IP:OK":"IP:FAIL", Font_6x8, White);

    // PING 1.1.1.1
    buf[0]=0; (void)ESP_AT_WaitEither("AT+PING=\"1.1.1.1\"", "OK", "ERROR", buf, sizeof(buf));
    ssd1306_SetCursor(0,20);
    ssd1306_WriteString(strstr(buf,"+PING:")?"PING:OK":"PING:FAIL", Font_6x8, White);

    // DNS
    buf[0]=0; (void)ESP_AT_WaitEither("AT+CIPDOMAIN=\"worldtimeapi.org\"", "OK", "ERROR", buf, sizeof(buf));
    ssd1306_SetCursor(64,20);
    ssd1306_WriteString(strstr(buf,"+CIPDOMAIN:")?"DNS:OK":"DNS:FAIL", Font_6x8, White);

    // TCP: example.com:80
    (void)ESP_AT_WaitEither("AT+CIPMODE=0", "OK", "ERROR", buf, sizeof(buf));
    (void)ESP_AT_WaitEither("AT+CIPMUX=0",  "OK", "ERROR", buf, sizeof(buf));
    buf[0]=0;
    (void)ESP_AT_WaitEither("AT+CIPSTART=\"TCP\",\"93.184.216.34\",80", "CONNECT", "ERROR", buf, sizeof(buf));
    ssd1306_SetCursor(0,30);
    ssd1306_WriteString(strstr(buf,"CONNECT")?"TCP:OK":"TCP:FAIL", Font_6x8, White);
    (void)ESP_AT_WaitEither("AT+CIPCLOSE", "OK", "ERROR", buf, sizeof(buf));

    ssd1306_SetCursor(0,56); ssd1306_WriteString("Btn=back", Font_6x8, White);
    ssd1306_UpdateScreen();
}
