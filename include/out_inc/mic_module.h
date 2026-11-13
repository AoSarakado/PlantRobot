/**
 * @file mic_module.h  GY-MAX4466 模拟麦克风（ADC1 GPIO2）
 */
#ifndef MIC_MODULE_H
#define MIC_MODULE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/* ---- 引脚（ESP32-S3，ADC1）---- */
#define MIC_ADC_GPIO       2

/* ---- 采样参数 ---- */
#define MIC_SAMPLE_RATE     16000   /* ★ 必须是 16000 */
#define MIC_FRAME_SAMPLES   480     /* 一帧 = 30ms @16kHz */

/* ---- 槽位 ---- */
#ifndef MIC_SLOT_IDX
#define MIC_SLOT_IDX        0       /* L/R=GND → 左槽 */
#endif

/* ---- 对外接口 ---- */
esp_err_t  mic_init(void);
bool       mic_rec_start(void);
bool       mic_play_start(void);
bool       mic_recording(void);
bool       mic_rec_playing(void);
uint32_t   mic_rec_samples(void);
/* 仅在录音结束且未回放时读取；返回已复制的样本数。 */
size_t     mic_rec_copy(int16_t *destination, size_t capacity);
bool       mic_monitor_toggle(void);
bool       mic_monitor_on(void);
uint32_t   mic_capture_ok(void);
uint32_t   mic_capture_fail(void);
int32_t    mic_last_peak(void);
int32_t    mic_probe_peak(void);
esp_err_t  mic_read_frame(int16_t *samples, size_t count);
void       mic_debug_raw(void);

#endif
