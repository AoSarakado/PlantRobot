#ifndef OLED_H
#define OLED_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define OLED_I2C_ADDR   0x3C

/* ===== 波特律动原版结构体定义（front.c 字库依赖此布局，勿改） ===== */
typedef struct {
    uint8_t w;
    uint8_t h;
    const uint8_t *chars;
} ASCIIFont;

typedef struct {
    uint8_t h;
    uint8_t w;
    const uint8_t *chars;
    uint8_t len;
    const ASCIIFont *ascii;
} Font;

typedef struct {
    uint8_t w;
    uint8_t h;
    const uint8_t *data;
} Image;

typedef enum {
    OLED_COLOR_NORMAL   = 0,
    OLED_COLOR_REVERSED = 1,
} OLED_ColorMode;

esp_err_t oled_init_on_bus(i2c_master_bus_handle_t bus);

void OLED_NewFrame(void);
void OLED_ShowFrame(void);
void OLED_SendCmd(uint8_t cmd);
void OLED_SetPixel(uint8_t x, uint8_t y, OLED_ColorMode color);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_ColorMode color);
void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);
void OLED_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode color);
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color);
void OLED_DrawFilledCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color);
void OLED_DrawImage(uint8_t x, uint8_t y, const Image *img, OLED_ColorMode color);
void OLED_PrintASCIIChar(uint8_t x, uint8_t y, char ch, const ASCIIFont *font, OLED_ColorMode color);
void OLED_PrintASCIIString(uint8_t x, uint8_t y, char *str, const ASCIIFont *font, OLED_ColorMode color);
void OLED_PrintString(uint8_t x, uint8_t y, char *str, const Font *font, OLED_ColorMode color);

#endif
