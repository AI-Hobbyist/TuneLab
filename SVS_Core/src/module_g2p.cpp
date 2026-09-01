#include "internal.hpp"

#if defined(_WIN32)
#define SVS_MODULE_API extern "C" __declspec(dllexport)
#else
#define SVS_MODULE_API extern "C" __attribute__((visibility("default")))
#endif

SVS_MODULE_API uint32_t svs_module_version(void) {
    return kModuleAbiVersion;
}