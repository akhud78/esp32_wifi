#pragma once
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ping_initialize(uint32_t interval_ms, uint32_t task_prio, char * target_host);

#ifdef __cplusplus
}
#endif
