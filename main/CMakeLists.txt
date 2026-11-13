set(SRCS
    main.c
    out_src/aht20.c
    out_src/audio_module.c
    out_src/cam_module.c
    out_src/front.c
    out_src/light_module.c
    out_src/mg90s.c
    out_src/mic_module.c
    out_src/oled.c
    soft_src/environment_state.c
    soft_src/asr_client.c
    soft_src/analog_capture.c
    soft_src/plant_assessment.c
    soft_src/tts_module.c
    soft_src/voice_interaction.c)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../include"
        "${CMAKE_CURRENT_LIST_DIR}/../include/out_inc"
        "${CMAKE_CURRENT_LIST_DIR}/../include/soft_inc"
    PRIV_REQUIRES
        esp_driver_i2c esp_driver_i2s esp_driver_gpio
        esp_driver_uart esp_driver_ledc esp_adc
        esp32-camera
        esp-sr esp_partition esp_wifi esp_netif esp_event esp_http_client nvs_flash
)

idf_component_get_property(esp_sr_dir espressif__esp-sr COMPONENT_DIR)

# 声音集来自组件管理器下载的官方 esp-sr，随 idf.py flash 写入 voice_data 分区。
esptool_py_flash_to_partition(flash "voice_data"
    "${esp_sr_dir}/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaole.dat")
