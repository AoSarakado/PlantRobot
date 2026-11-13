/**
 * @file cam_module.c  OV2640 JPEG QVGA，帧缓冲放 PSRAM
 */
#include "cam_module.h"
#include <stdio.h>
#include <string.h>

static const camera_config_t s_cfg = {
    .pin_sccb_sda = 4,  .pin_sccb_scl = 5,
    .pin_pwdn     = 47, .pin_reset    = -1,
    .pin_xclk     = -1, .xclk_freq_hz = 10000000,

    .pin_d7 = 16, .pin_d6 = 17, .pin_d5 = 18, .pin_d4 = 12,
    .pin_d3 = 10, .pin_d2 = 8,  .pin_d1 = 9,  .pin_d0 = 11,
    .pin_vsync = 6, .pin_href = 7, .pin_pclk = 13,

    .ledc_channel = LEDC_CHANNEL_1,
    .ledc_timer   = LEDC_TIMER_1,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count     = 2,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_LATEST,
};

esp_err_t cam_module_init(void)
{
    esp_err_t err = esp_camera_init(&s_cfg);
    if (err == ESP_OK) {
        sensor_t *s = esp_camera_sensor_get();
        s->set_vflip(s, 1);
    }
    return err;
}

camera_fb_t *cam_module_grab(void)           { return esp_camera_fb_get(); }
void cam_module_release(camera_fb_t *fb)     { esp_camera_fb_return(fb); }

/* 用 printf（不受日志级别影响），直接反馈帧完整性 */
void cam_dump_info(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { printf("cam: grab timeout\r\n"); return; }

    const uint8_t *p = fb->buf;
    size_t n = fb->len;
    bool head_ok = (n >= 2 && p[0] == 0xFF && p[1] == 0xD8);
    bool tail_ok = (n >= 2 && p[n-2] == 0xFF && p[n-1] == 0xD9);

    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += p[i];

    printf("cam: %ux%u %uB\r\n", fb->width, fb->height, (unsigned)n);
    printf("cam: head %s tail %s\r\n", head_ok ? "OK" : "BAD", tail_ok ? "OK" : "BAD");
    printf("cam: sum=0x%08lX\r\n", (unsigned long)sum);

    esp_camera_fb_return(fb);
}
