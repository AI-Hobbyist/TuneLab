#include "svs_plugin.h"

namespace {

const unsigned char kAvatar[] = {0x89, 0x50, 0x4e, 0x47};
const unsigned char kPortrait[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a};

const char* plugin_name() { return "SVS Native Test Engine"; }
const char* plugin_version() { return "1.0.0"; }

size_t voice_source_count() { return 1; }

svs_status voice_source_get(size_t index, svs_voice_source_info* out_info) {
    if (index != 0 || out_info == nullptr) return SVS_ERR_NOT_FOUND;
    *out_info = {
        {"native-alice", 12},
        {"Native Alice", 12},
        {"Native voice source for loader validation", 40},
        {{"image/png", 9}, {nullptr, 0}, kAvatar, sizeof(kAvatar)},
        {{"image/png", 9}, {nullptr, 0}, kPortrait, sizeof(kPortrait)},
    };
    return SVS_OK;
}

size_t format_count() { return 1; }
const char* format_name(size_t index) { return index == 0 ? "SVS Test Project" : nullptr; }
const char* format_extension(size_t index) { return index == 0 ? "svst" : nullptr; }

const svs_plugin_vtable kPlugin = {
    sizeof(svs_plugin_vtable),
    SVS_PLUGIN_API_VERSION,
    plugin_name,
    plugin_version,
    voice_source_count,
    voice_source_get,
    format_count,
    format_name,
    format_extension,
};

} // namespace

extern "C" SVS_PLUGIN_API const svs_plugin_vtable* svs_plugin_get_api(
    uint32_t host_api_version, uint32_t* out_plugin_api_version) {
    if (out_plugin_api_version != nullptr) *out_plugin_api_version = SVS_PLUGIN_API_VERSION;
    return host_api_version == SVS_PLUGIN_API_VERSION ? &kPlugin : nullptr;
}