#ifndef SVS_PLUGIN_H
#define SVS_PLUGIN_H

#include "svs_core.h"

#if defined(_WIN32)
#define SVS_PLUGIN_API __declspec(dllexport)
#else
#define SVS_PLUGIN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SVS_PLUGIN_API_VERSION 0x00010000u

typedef struct svs_plugin_vtable {
    uint32_t size;
    uint32_t api_version;
    const char* (*name)(void);
    const char* (*version)(void);
    size_t (*voice_source_count)(void);
    svs_status (*voice_source_get)(size_t index, svs_voice_source_info* out_info);
    size_t (*format_count)(void);
    const char* (*format_name)(size_t index);
    const char* (*format_extension)(size_t index);
} svs_plugin_vtable;

typedef const svs_plugin_vtable* (*svs_plugin_get_api_fn)(uint32_t host_api_version,
                                                           uint32_t* out_plugin_api_version);

#ifdef __cplusplus
}
#endif

#endif