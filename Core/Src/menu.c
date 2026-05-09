#include <ldr.h>
#include "menu.h"
#include <stdio.h>

const char* menu_items[MENU_COUNT] = {
	"Time",
    "Weather",
    "Alarm",
    "Stopwatch",
    "Brightness"
};

volatile int menu_index = 0;
volatile MenuState menu_state = MENU_STATE_MAIN;

void show_menu(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);

    if (menu_state == MENU_STATE_MAIN) {
        //ssd1306_WriteString("Menu:", Font_7x10, White);
        for (int i = 0; i < MENU_COUNT; i++) {
            uint8_t y = i * 14;
            ssd1306_SetCursor(0, y);
            if (i == menu_index) {
                ssd1306_WriteString(">", Font_7x10, White);
                ssd1306_SetCursor(12, y);
            }
            ssd1306_WriteString((char*)menu_items[i], Font_6x8, White);
        }
    }
    else if (menu_state == MENU_STATE_BRIGHTNESS) {
        const char* brightness_items[2] = {"Auto", "Manual"};
        ssd1306_WriteString("Brightness:", Font_7x10, White);
        for (int i = 0; i < 2; i++) {
            uint8_t y = 22 + i * 16;
            ssd1306_SetCursor(0, y);
            if (i == menu_index) {
                ssd1306_WriteString(">", Font_6x8, White);
                ssd1306_SetCursor(12, y);
            }
            ssd1306_WriteString((char*)brightness_items[i], Font_6x8, White);
        }
    }
    else if (menu_state == MENU_STATE_BRIGHTNESS_MANUAL) {
        char buf[32];
        sprintf(buf, "Brightness: %d", current_brightness);
        ssd1306_WriteString("Adjust:", Font_7x10, White);
        ssd1306_SetCursor(0, 30);
        ssd1306_WriteString(buf, Font_6x8, White);
        ssd1306_SetCursor(0, 50);
        ssd1306_WriteString("Press to confirm", Font_6x8, White);
    }

    ssd1306_UpdateScreen();
}
