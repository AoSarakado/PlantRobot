/**
 * @file tts_module.h  esp-tts 封装：分区加载 + 队列化合成 + 外部播放回调
 *
 * 设计原则：这个模块不认识 I2S。合成出来的 PCM 交给调用者给的 sink，
 * 所以可以直接挂到已有工程的 audio_play_frame() 上，不跟别人抢 I2S 通道。
 *
 * 非阻塞：tts_say() 只入队，立刻返回。真正的合成在内部任务里跑。
 */
#ifndef TTS_MODULE_H
#define TTS_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 播放回调：单声道 16bit，samples 是采样点数（字节数 = samples * 2）
 * 签名对齐 esp_err_t name(const int16_t *, size_t)，audio_play_frame 可直接传 */
typedef esp_err_t (*tts_sink_t)(const int16_t *pcm, size_t samples);

/* 收尾回调，可为 NULL。签名刻意对齐 esp_err_t name(void)，
 * 这样 audio_play_end 这种现成函数可以直接传进来。返回值被忽略。 */
typedef esp_err_t (*tts_end_t)(void);

/**
 * @param partition_label  声音集所在分区名，通常是 "voice_data"
 * @param sink             PCM 出口，不能为 NULL
 * @param on_end           每段结束回调，可为 NULL
 * @return ESP_OK / ESP_ERR_NOT_FOUND(分区) / ESP_ERR_INVALID_ARG / ESP_FAIL
 */
esp_err_t tts_module_init(const char *partition_label, tts_sink_t sink, tts_end_t on_end);

/**
 * 入队一段 UTF-8 中文，立刻返回。
 * @param speed 0(最慢) ~ 5(最快)，超范围自动夹紧
 * @return true 入队成功；false 未初始化 / 文本空 / 过长 / 队列满
 */
bool tts_say(const char *utf8_text, uint8_t speed);

/** 队列里还有待合成的，或者正在念 */
bool     tts_busy(void);
/** 已完成的段数 / 因为队列满或过长被丢弃的次数（调试用） */
uint32_t tts_done_count(void);
uint32_t tts_drop_count(void);
/** 队列当前排队数 */
uint32_t tts_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* TTS_MODULE_H */
