#ifndef MY_LOGS_H
#define MY_LOGS_H

#include "config.h"

// Disabilita TUTTI i miei log se ENABLE_DEBUG_LOGS = 0
#if !ENABLE_DEBUG_LOGS
    #undef ESP_LOGI
    #undef ESP_LOGD
    #undef ESP_LOGW
    #undef ESP_LOGE
    #define ESP_LOGI(tag, format, ...) 
    #define ESP_LOGD(tag, format, ...) 
    #define ESP_LOGW(tag, format, ...) 
    #define ESP_LOGE(tag, format, ...) 
#endif

#endif