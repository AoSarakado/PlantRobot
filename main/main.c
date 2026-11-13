/**
 * @file main.c
 * @brief 串口命令 → 直接 printf 反馈
 *
 * 引脚全部保持不变：
 *   I2C0: SDA=39 SCL=40   光敏 AO=1   舵机=15
 *   功放 I2S0: BCLK=14 LRC=21 DIN=45
 *   GY-MAX4466 麦克风模拟 OUT=GPIO2（ADC1）
 *   摄像头: SDA=4 SCL=5 VS=6 HR=7 PCLK=13 PWDN=47 D0~D7=11,9,8,10,12,18,17,16
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"

#include "oled.h"
#include "aht20.h"
#include "light_module.h"
#include "audio_module.h"
#include "mg90s.h"
#include "cam_module.h"
#include "mic_module.h"
#include "tts_module.h"
#include "environment_state.h"
#include "voice_interaction.h"
#include "plant_assessment.h"
#include "asr_client.h"

/* 字库定义在 front.c 里 */
extern const ASCIIFont afont16x8;

#define BUF_LEN     192
#define CORE_MAIN   1
#define CORE_WORK   0

/* ==================== I2C 引脚 ==================== */
#define I2C_SDA_GPIO    39
#define I2C_SCL_GPIO    40


static i2c_master_bus_handle_t s_bus = NULL;

static void i2c_bus_recover(gpio_num_t sda, gpio_num_t scl)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(scl, 0);  esp_rom_delay_us(5);
        gpio_set_level(scl, 1);  esp_rom_delay_us(5);
    }
    gpio_set_level(sda, 0);  esp_rom_delay_us(5);
    gpio_set_level(scl, 1);  esp_rom_delay_us(5);
    gpio_set_level(sda, 1);  esp_rom_delay_us(5);
}

static void print_fix(const char *label, float v, const char *unit)
{
    int neg = v < 0;
    float a = neg ? -v : v;
    printf("%s: %s%d.%d%s\r\n", label, neg ? "-" : "",
           (int)a, (int)(a * 10) % 10, unit);
}

static void main_task(void *arg)
{
    char line[24];
    while (1) {
        environment_t env = environment_snapshot();
        OLED_NewFrame();
        OLED_PrintASCIIString(0, 0, "Plant Monitor", &afont16x8, OLED_COLOR_NORMAL);
        if (env.light_valid) snprintf(line, sizeof(line), "light: %d%%", env.brightness);
        else snprintf(line, sizeof(line), "light: --");
        OLED_PrintASCIIString(0, 20, line, &afont16x8, OLED_COLOR_NORMAL);
        float temp = env.temperature;
        float abs_temp = temp < 0 ? -temp : temp;
        if (env.temperature_valid)
            snprintf(line, sizeof(line), "T:%s%d.%dC H:%d%%",
                     temp < 0 ? "-" : "", (int)abs_temp,
                     (int)(abs_temp * 10) % 10, (int)env.humidity);
        else snprintf(line, sizeof(line), "T:-- H:--");
        OLED_PrintASCIIString(0, 36, line, &afont16x8, OLED_COLOR_NORMAL);
        OLED_ShowFrame();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void cam_drain_task(void *arg)
{
    for (;;) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void uart_task(void *arg)
{
    char buf[BUF_LEN];
    int len = 0;
    uint8_t ch;

    printf("\r\n==== ready. cmds: tone lite temp state scan mic micr micp rec play stt cam serv sv90 ====\r\n");

    while (1) {
        if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100)) != 1) continue;

        if (ch == '\r' || ch == '\n') {
            buf[len] = '\0'; len = 0;
            if (buf[0] == '\0') continue;

            if (!strcmp(buf, "tone")) {
                play_test_tone();
                printf("tone done\r\n");
            }
            else if (!strcmp(buf, "lite")) {
                environment_t env = environment_snapshot();
                if (env.light_valid) printf("brightness: %d%% (raw=%d)\r\n", env.brightness, env.light_raw);
                else printf("light: no valid sample\r\n");
            }
            else if (!strncmp(buf, "say ", 4) && buf[4] != '\0') {
                if (tts_say(buf + 4, 3)) printf("say: queued\r\n");
                else                     printf("say: queue full\r\n");
            }
            else if (!strcmp(buf, "temp")) {
                environment_t env = environment_snapshot();
                if (env.temperature_valid) {
                    print_fix("temp", env.temperature, " C");
                    print_fix("humi", env.humidity, " %RH");
                } else {
                    printf("aht20: no valid sample (run 'scan')\r\n");
                }
            }
            else if (!strcmp(buf, "state")) {
                environment_t env = environment_snapshot();
                plant_assessment_t assessment = plant_assess(&env, PLANT_UNKNOWN);
                if (assessment.temperature == PLANT_TEMP_UNKNOWN && !env.temperature_valid)
                    printf("state: no valid temperature sample\r\n");
                else
                    printf("state: %u/%u references below minimum; %u/%u in documented preferred range\r\n",
                           assessment.cold_count, assessment.reference_count,
                           assessment.preferred_count, assessment.reference_count);
                printf("state: light and humidity recorded, species suitability not calibrated\r\n");
            }
            else if (!strcmp(buf, "scan")) {
                printf("scanning I2C...\r\n");
                for (uint8_t addr = 0x03; addr < 0x78; addr++) {
                    if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
                        printf("  device at 0x%02X\r\n", addr);
                    }
                }
                printf("scan done (want 0x3C=OLED, 0x38=AHT20)\r\n");
            }
            else if (!strcmp(buf, "mic")) {
                if (mic_monitor_on()) {
                    int32_t pk = mic_last_peak();
                    if (pk < 0) printf("mic: no frame yet\r\n");
                    else        printf("mic: peak %ld/32767 (live)\r\n", (long)pk);
                } else {
                    int32_t pk = mic_probe_peak();
                    if (pk < 0) printf("mic: probe timeout\r\n");
                    else        printf("mic: peak %ld/32767\r\n", (long)pk);
                }
            }
            else if (!strcmp(buf, "micr")) {
                mic_debug_raw();
            }
            else if (!strcmp(buf, "micp")) {
                if (mic_recording())        printf("micp: recording, wait\r\n");
                else if (mic_rec_playing()) printf("micp: playing, wait\r\n");
                else {
                    bool on = mic_monitor_toggle();
                    printf("micp: live monitor %s\r\n", on ? "ON, speak..." : "OFF");
                }
            }
            else if (!strcmp(buf, "rec")) {
                if (asr_client_busy())      printf("rec: stt in progress\r\n");
                else if (mic_recording())   printf("rec: already recording\r\n");
                else if (mic_rec_playing()) printf("rec: playing, wait\r\n");
                else if (mic_monitor_on())  printf("rec: stop micp first\r\n");
                else if (mic_rec_start())   { /* 任务自己打印 rec: done */ }
                else                        printf("rec: start FAIL\r\n");
            }
            else if (!strcmp(buf, "play")) {
                if (asr_client_busy())      printf("play: stt in progress\r\n");
                else if (mic_rec_playing()) printf("play: already playing\r\n");
                else if (mic_recording())   printf("play: recording, wait\r\n");
                else if (mic_monitor_on())  printf("play: stop micp first\r\n");
                else if (mic_rec_samples() == 0) printf("play: buffer empty, run 'rec' first\r\n");
                else if (mic_play_start())  { /* 任务自己打印 play: done */ }
                else                        printf("play: start FAIL\r\n");
            }
            else if (!strcmp(buf, "cam")) {
                cam_dump_info();
            }
            else if (!strcmp(buf, "stt")) {
                esp_err_t err = asr_client_start();
                if (err == ESP_OK) printf("stt: recording...\r\n");
                else printf("stt: start failed 0x%x (check Wi-Fi and ASR URL)\r\n", err);
            }
            else if (!strcmp(buf, "serv")) {
                printf("servo sweep\r\n");
                servo_set_angle(0, 600);
                servo_set_angle(90, 600);
                servo_set_angle(180, 600);
                servo_set_angle(90, 600);
                printf("servo done\r\n");
            }
            else if (!strncmp(buf, "sv", 2) && buf[2] != '\0') {
                int ang = atoi(buf + 2);
                if (ang < 0 || ang > 180) printf("servo: sv0~sv180 only\r\n");
                else { servo_set_angle(ang, 600); printf("servo -> %d\r\n", ang); }
            }
            else {
                printf("? cmds: tone lite temp scan mic micr micp rec play cam serv sv0~180\r\n");
            }
            continue;
        }
        if (len < BUF_LEN - 1) buf[len++] = (char)ch;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(environment_state_init());
    i2c_bus_recover(I2C_SDA_GPIO, I2C_SCL_GPIO);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    if (oled_init_on_bus(s_bus) == ESP_OK) printf("OLED ok\r\n");
    else printf("OLED FAIL\r\n");

    if (aht20_init(s_bus) == ESP_OK) printf("AHT20 ok\r\n");
    else printf("AHT20 FAIL (check wiring/VCC, cmd 'scan')\r\n");

    printf("light: %s\r\n", light_init() == ESP_OK ? "ok" : "FAIL");
    printf("audio: %s\r\n", audio_init() == ESP_OK ? "ok" : "FAIL");

    bool tts_ready = tts_module_init("voice_data", audio_play_frame, audio_play_end) == ESP_OK;
    if (!tts_ready) {
        printf("tts  : FAIL\r\n");
    } else {
        printf("tts  : ok\r\n");
    }

    printf("servo: %s\r\n", servo_init() == ESP_OK ? "ok" : "FAIL");

    bool mic_ready = mic_init() == ESP_OK;
    if (mic_ready) {
        printf("mic  : ok\r\n");
        /* micp 开时才创建任务，这里无需常驻任务 */
    } else {
        printf("mic  : FAIL (I2S1 init failed?)\r\n");
    }

    uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &cfg);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    configASSERT(xTaskCreatePinnedToCore(main_task, "main", 4096, NULL, 3, NULL, CORE_MAIN) == pdPASS);
    configASSERT(xTaskCreatePinnedToCore(uart_task, "uart", 8192, NULL, 5, NULL, CORE_WORK) == pdPASS);
    configASSERT(xTaskCreatePinnedToCore(environment_sensor_task, "sensor", 4096, NULL, 4, NULL, CORE_WORK) == pdPASS);
    if (mic_ready && tts_ready) {
        configASSERT(xTaskCreatePinnedToCore(voice_interaction_task, "voice", 8192, NULL, 3, NULL, CORE_WORK) == pdPASS);
    }

    if (cam_module_init() == ESP_OK) {
        printf("cam: ok\r\n");
        configASSERT(xTaskCreatePinnedToCore(cam_drain_task, "camdrain", 3072, NULL, 2, NULL, CORE_WORK) == pdPASS);
    } else {
        printf("cam: FAIL (PSRAM? cmd 'cam')\r\n");
    }
}
