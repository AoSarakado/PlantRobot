#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include <stddef.h>
#include "esp_err.h"

/* 自建服务端部署模型，或调用云端模型服务；ESP32-S3 不运行 LLM。 */
typedef enum {
    LLM_PROVIDER_SELF_HOSTED,
    LLM_PROVIDER_CLOUD,
} llm_provider_kind_t;

typedef struct {
    const char *transcript;
    const char *plant_context;
} llm_request_t;

typedef struct {
    esp_err_t (*generate)(const llm_request_t *request, char *reply, size_t reply_size);
} llm_provider_t;

#endif
