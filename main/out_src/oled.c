/**
 * @file oled.c  SSD1306 128x64 驱动（I2C 在已有总线上挂载）
 */
#include "oled.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define OLED_SetBlock_pub OLED_SetBlock
#define OLED_PAGE   8
#define OLED_ROW    8 * OLED_PAGE
#define OLED_COLUMN 128

static uint8_t OLED_GRAM[OLED_PAGE][OLED_COLUMN];
static i2c_master_dev_handle_t s_dev;

static void OLED_Send(uint8_t *data, uint8_t len)
{
    i2c_master_transmit(s_dev, data, len, 100);
}

void OLED_SendCmd(uint8_t cmd)
{
    static uint8_t buf[2] = {0x00, 0};
    buf[1] = cmd;
    OLED_Send(buf, 2);
}

esp_err_t oled_init_on_bus(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OLED_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev),
                        "OLED", "add device");

    vTaskDelay(pdMS_TO_TICKS(100));

    OLED_SendCmd(0xAE);
    OLED_SendCmd(0x20); OLED_SendCmd(0x10);
    OLED_SendCmd(0xB0);
    OLED_SendCmd(0xC8);
    OLED_SendCmd(0x00); OLED_SendCmd(0x10);
    OLED_SendCmd(0x40);
    OLED_SendCmd(0x81); OLED_SendCmd(0xDF);
    OLED_SendCmd(0xA1);
    OLED_SendCmd(0xA6);
    OLED_SendCmd(0xA8); OLED_SendCmd(0x3F);
    OLED_SendCmd(0xA4);
    OLED_SendCmd(0xD3); OLED_SendCmd(0x00);
    OLED_SendCmd(0xD5); OLED_SendCmd(0xF0);
    OLED_SendCmd(0xD9); OLED_SendCmd(0x22);
    OLED_SendCmd(0xDA); OLED_SendCmd(0x12);
    OLED_SendCmd(0xDB); OLED_SendCmd(0x20);
    OLED_SendCmd(0x8D); OLED_SendCmd(0x14);
    OLED_NewFrame();
    OLED_ShowFrame();
    OLED_SendCmd(0xAF);
    return ESP_OK;
}

void OLED_NewFrame(void) { memset(OLED_GRAM, 0, sizeof(OLED_GRAM)); }

void OLED_ShowFrame(void)
{
    static uint8_t buf[OLED_COLUMN + 1];
    buf[0] = 0x40;
    for (uint8_t i = 0; i < OLED_PAGE; i++) {
        OLED_SendCmd(0xB0 + i);
        OLED_SendCmd(0x00);
        OLED_SendCmd(0x10);
        memcpy(buf + 1, OLED_GRAM[i], OLED_COLUMN);
        OLED_Send(buf, OLED_COLUMN + 1);
    }
}

void OLED_SetPixel(uint8_t x, uint8_t y, OLED_ColorMode color)
{
    if (x >= OLED_COLUMN || y >= OLED_ROW) return;
    if (!color) OLED_GRAM[y / 8][x] |=  (1 << (y % 8));
    else        OLED_GRAM[y / 8][x] &= ~(1 << (y % 8));
}

static void OLED_SetByte_Fine(uint8_t page, uint8_t col, uint8_t data,
                              uint8_t start, uint8_t end, OLED_ColorMode color)
{
    static uint8_t temp;
    if (page >= OLED_PAGE || col >= OLED_COLUMN) return;
    if (color) data = ~data;
    temp = data | (0xff << (end + 1)) | (0xff >> (8 - start));
    OLED_GRAM[page][col] &= temp;
    temp = data & ~(0xff << (end + 1)) & ~(0xff >> (8 - start));
    OLED_GRAM[page][col] |= temp;
}

static void OLED_SetBits_Fine(uint8_t x, uint8_t y, uint8_t data,
                              uint8_t len, OLED_ColorMode color)
{
    uint8_t page = y / 8, bit = y % 8;
    if (bit + len > 8) {
        OLED_SetByte_Fine(page, x, data << bit, bit, 7, color);
        OLED_SetByte_Fine(page + 1, x, data >> (8 - bit), 0, len + bit - 1 - 8, color);
    } else {
        OLED_SetByte_Fine(page, x, data << bit, bit, bit + len - 1, color);
    }
}

static void OLED_SetBits(uint8_t x, uint8_t y, uint8_t data, OLED_ColorMode color)
{
    uint8_t page = y / 8, bit = y % 8;
    OLED_SetByte_Fine(page, x, data << bit, bit, 7, color);
    if (bit) OLED_SetByte_Fine(page + 1, x, data >> (8 - bit), 0, bit - 1, color);
}

static void OLED_SetBlock(uint8_t x, uint8_t y, const uint8_t *data,
                          uint8_t w, uint8_t h, OLED_ColorMode color)
{
    uint8_t fullRow = h / 8, partBit = h % 8;
    for (uint8_t i = 0; i < w; i++)
        for (uint8_t j = 0; j < fullRow; j++)
            OLED_SetBits(x + i, y + j * 8, data[i + j * w], color);
    if (partBit) {
        uint16_t fullNum = w * fullRow;
        for (uint8_t i = 0; i < w; i++)
            OLED_SetBits_Fine(x + i, y + fullRow * 8, data[fullNum + i], partBit, color);
    }
}

void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, OLED_ColorMode color)
{
    static uint8_t t;
    if (x1 == x2) {
        if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
        for (uint8_t y = y1; y <= y2; y++) OLED_SetPixel(x1, y, color);
    } else if (y1 == y2) {
        if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
        for (uint8_t x = x1; x <= x2; x++) OLED_SetPixel(x, y1, color);
    } else {
        int16_t dx = x2 - x1, dy = y2 - y1;
        int16_t ux = ((dx > 0) << 1) - 1, uy = ((dy > 0) << 1) - 1;
        int16_t x = x1, y = y1, eps = 0;
        dx = abs(dx); dy = abs(dy);
        if (dx > dy) {
            for (x = x1; x != x2; x += ux) {
                OLED_SetPixel(x, y, color);
                eps += dy;
                if ((eps << 1) >= dx) { y += uy; eps -= dx; }
            }
        } else {
            for (y = y1; y != y2; y += uy) {
                OLED_SetPixel(x, y, color);
                eps += dx;
                if ((eps << 1) >= dy) { x += ux; eps -= dy; }
            }
        }
    }
}

void OLED_DrawRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode c)
{
    OLED_DrawLine(x, y, x + w, y, c);
    OLED_DrawLine(x, y + h, x + w, y + h, c);
    OLED_DrawLine(x, y, x, y + h, c);
    OLED_DrawLine(x + w, y, x + w, y + h, c);
}

void OLED_DrawFilledRectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h, OLED_ColorMode c)
{
    for (uint8_t i = 0; i < h; i++) OLED_DrawLine(x, y + i, x + w, y + i, c);
}

void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color)
{
    int16_t a = 0, b = r, di = 3 - (r << 1);
    while (a <= b) {
        OLED_SetPixel(x - b, y - a, color); OLED_SetPixel(x + b, y - a, color);
        OLED_SetPixel(x - a, y + b, color); OLED_SetPixel(x - b, y - a, color);
        OLED_SetPixel(x - a, y - b, color); OLED_SetPixel(x + b, y + a, color);
        OLED_SetPixel(x + a, y - b, color); OLED_SetPixel(x + a, y + b, color);
        OLED_SetPixel(x - b, y + a, color);
        a++;
        if (di < 0) di += 4 * a + 6;
        else { di += 10 + 4 * (a - b); b--; }
        OLED_SetPixel(x + a, y + b, color);
    }
}

void OLED_DrawFilledCircle(uint8_t x, uint8_t y, uint8_t r, OLED_ColorMode color)
{
    int16_t a = 0, b = r, di = 3 - (r << 1);
    while (a <= b) {
        for (int16_t i = x - b; i <= x + b; i++) {
            OLED_SetPixel(i, y + a, color); OLED_SetPixel(i, y - a, color);
        }
        for (int16_t i = x - a; i <= x + a; i++) {
            OLED_SetPixel(i, y + b, color); OLED_SetPixel(i, y - b, color);
        }
        a++;
        if (di < 0) di += 4 * a + 6;
        else { di += 10 + 4 * (a - b); b--; }
    }
}

void OLED_DrawImage(uint8_t x, uint8_t y, const Image *img, OLED_ColorMode color)
{
    OLED_SetBlock(x, y, img->data, img->w, img->h, color);
}

void OLED_PrintASCIIChar(uint8_t x, uint8_t y, char ch, const ASCIIFont *font, OLED_ColorMode color)
{
    OLED_SetBlock(x, y, font->chars + (ch - ' ') * (((font->h + 7) / 8) * font->w),
                  font->w, font->h, color);
}

void OLED_PrintASCIIString(uint8_t x, uint8_t y, char *str, const ASCIIFont *font, OLED_ColorMode color)
{
    uint8_t x0 = x;
    while (*str) {
        OLED_PrintASCIIChar(x0, y, *str, font, color);
        x0 += font->w;
        str++;
    }
}
/* 中文/混合字符串（字模在 front.c 的 Font 里） */
static uint8_t _OLED_GetUTF8Len(char *s)
{
    if ((s[0] & 0x80) == 0x00) return 1;
    else if ((s[0] & 0xE0) == 0xC0) return 2;
    else if ((s[0] & 0xF0) == 0xE0) return 3;
    else if ((s[0] & 0xF8) == 0xF0) return 4;
    return 0;
}

void OLED_PrintString(uint8_t x, uint8_t y, char *str, const Font *font, OLED_ColorMode color)
{
    uint16_t i = 0;
    uint8_t oneLen = (((font->h + 7) / 8) * font->w) + 4;
    uint8_t found, utf8Len;
    uint8_t *head;
    while (str[i]) {
        found = 0;
        utf8Len = _OLED_GetUTF8Len(str + i);
        if (utf8Len == 0) break;

        for (uint8_t j = 0; j < font->len; j++) {
            head = (uint8_t *)(font->chars) + (j * oneLen);
            if (memcmp(str + i, head, utf8Len) == 0) {
                OLED_SetBlock(x, y, head + 4, font->w, font->h, color);
                x += font->w;
                i += utf8Len;
                found = 1;
                break;
            }
        }
        if (found == 0) {
            OLED_PrintASCIIChar(x, y, utf8Len == 1 ? str[i] : ' ', font->ascii, color);
            x += font->ascii->w;
            i += utf8Len;
        }
    }
}
