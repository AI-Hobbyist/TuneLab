#include "internal.hpp"
#include "svs_plugin.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <regex>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#if defined(SVS_CORE_ENABLE_DOTNET_HOSTING)
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>
#endif
#else
#include <dlfcn.h>
#endif

namespace {

using module_version_fn = uint32_t (*)();

void close_modules(svs_context* context) {
    for (void* module : context->modules) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(module));
#else
        dlclose(module);
#endif
    }
    context->modules.clear();
}

void close_library(void* module) {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

std::filesystem::path core_directory() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&svs_context_create), &module);
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#else
    Dl_info info{};
    dladdr(reinterpret_cast<void*>(&svs_context_create), &info);
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

svs_status load_module(svs_context* context, const char* name) {
    const auto path = core_directory() / name;
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(path.c_str());
    auto version = module == nullptr ? nullptr : reinterpret_cast<module_version_fn>(
        GetProcAddress(module, "svs_module_version"));
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto version = module == nullptr ? nullptr : reinterpret_cast<module_version_fn>(
        dlsym(module, "svs_module_version"));
#endif
    if (module == nullptr || version == nullptr) {
        if (module != nullptr) {
#if defined(_WIN32)
            FreeLibrary(module);
#else
            dlclose(module);
#endif
        }
        set_error(context, "Required SVS Core module could not be loaded.");
        return SVS_ERR_MODULE_LOAD;
    }
    if (version() != kModuleAbiVersion) {
#if defined(_WIN32)
        FreeLibrary(module);
#else
        dlclose(module);
#endif
        set_error(context, "Required SVS Core module has an incompatible ABI version.");
        return SVS_ERR_MODULE_VERSION;
    }
    context->modules.push_back(module);
    return SVS_OK;
}

svs_string_view string_view(const std::string& value) {
    return {value.data(), value.size()};
}

double ticks_per_beat(const svs_time_signature& signature) {
    return SVS_PPQ * 4.0 / signature.denominator;
}

double ticks_per_bar(const svs_time_signature& signature) {
    return ticks_per_beat(signature) * signature.numerator;
}

struct meter_position {
    const svs_time_signature* signature;
    double tick;
    double beat;
};

meter_position meter_at_bar(const svs_score* score, int32_t bar) {
    const auto& signatures = score->time_signatures;
    size_t index = 0;
    double tick = 0;
    double beat = 0;
    for (size_t next = 1; next < signatures.size() && signatures[next].bar <= bar; ++next) {
        const auto& current = signatures[index];
        const int32_t bars = signatures[next].bar - current.bar;
        tick += bars * ticks_per_bar(current);
        beat += bars * current.numerator;
        index = next;
    }
    return {&signatures[index], tick, beat};
}

meter_position meter_at_tick(const svs_score* score, double tick) {
    if (tick < 0) return {&score->time_signatures.front(), 0, 0};
    meter_position result = meter_at_bar(score, 0);
    for (size_t index = 1; index < score->time_signatures.size(); ++index) {
        const meter_position candidate = meter_at_bar(score, score->time_signatures[index].bar);
        if (candidate.tick > tick) break;
        result = candidate;
    }
    return result;
}

double seconds_at_tempo_index(const svs_score* score, size_t target_index) {
    double seconds = 0;
    for (size_t index = 1; index <= target_index; ++index) {
        const auto& previous = score->tempos[index - 1];
        seconds += (score->tempos[index].tick - previous.tick) * 60.0 /
                   (previous.bpm * SVS_PPQ);
    }
    return seconds;
}

double score_end_tick(const svs_score* score) {
    double result = 0;
    for (const auto& track : score->tracks) {
        for (const auto& part : track->parts) {
            for (const auto& note : part->notes) {
                result = (std::max)(result, note->pos + note->dur);
            }
        }
    }
    return result;
}

svs_context* context_for(svs_part* part) {
    return part->parent->parent->context;
}

void refresh_note_view(svs_part* part) {
    part->note_view.clear();
    part->note_view.reserve(part->notes.size());
    for (const auto& note : part->notes) part->note_view.push_back(note.get());
    std::sort(part->note_view.begin(), part->note_view.end(), [](const svs_note* left, const svs_note* right) {
        return left->pos == right->pos ? left < right : left->pos < right->pos;
    });
}

void append_public_phonemes(std::vector<svs_phoneme>& output, const std::vector<phoneme_data>& source) {
    for (const auto& phoneme : source) {
        output.push_back({string_view(phoneme.symbol), phoneme.duration, phoneme.stretch_weight});
    }
}

bool valid_phoneme(const svs_phoneme* phoneme) {
    return phoneme != nullptr && phoneme->symbol.data != nullptr && std::isfinite(phoneme->duration) &&
           std::isfinite(phoneme->stretch_weight) && phoneme->duration >= 0;
}

phoneme_data copy_phoneme(const svs_phoneme& phoneme) {
    return {std::string(phoneme.symbol.data, phoneme.symbol.size), phoneme.duration, phoneme.stretch_weight};
}

std::vector<std::string> split_lyrics(std::string_view text) {
    std::vector<std::string> result;
    for (size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x20 || std::string_view(".,!?;:()[]{}\\/\"'%").find(static_cast<char>(first)) != std::string_view::npos) {
            ++index;
            continue;
        }
        size_t length = 1;
        if ((first & 0xF0) == 0xF0) length = 4;
        else if ((first & 0xE0) == 0xE0) length = 3;
        else if ((first & 0xC0) == 0xC0) length = 2;
        if (index + length > text.size()) break;
        if (first == 0xE3 && length == 3 && index + 6 <= text.size()) {
            const std::string_view following = text.substr(index + 3, 3);
            const unsigned char second = static_cast<unsigned char>(following[1]);
            const unsigned char third = static_cast<unsigned char>(following[2]);
            const bool hiragana_small = second == 0x82 &&
                (third == 0x83 || third == 0x85 || third == 0x87 || third == 0xA1 ||
                 third == 0xA3 || third == 0xA5 || third == 0xA7 || third == 0xA9);
            const bool katakana_small = second == 0x83 &&
                (third == 0xA3 || third == 0xA5 || third == 0xA7);
            if (static_cast<unsigned char>(following[0]) == 0xE3 && (hiragana_small || katakana_small)) {
                length += 3;
            }
        }
        if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')) {
            size_t end = index + 1;
            while (end < text.size() && ((text[end] >= 'A' && text[end] <= 'Z') ||
                                         (text[end] >= 'a' && text[end] <= 'z'))) ++end;
            result.emplace_back(text.substr(index, end - index));
            index = end;
        } else {
            result.emplace_back(text.substr(index, length));
            index += length;
        }
    }
    return result;
}

std::pair<std::string, std::vector<std::string>> pronunciation_for(const std::string& lyric) {
    static const std::unordered_map<std::string, std::pair<const char*, const char*>> pinyin = {
        {"\xE4\xBD\xA0", {"ni3", "ni3"}}, {"\xE5\xA5\xBD", {"hao3", "hao3"}},
        {"\xE6\x88\x91", {"wo3", "wo3"}}, {"\xE6\x98\xAF", {"shi4", "shi4"}},
        {"\xE7\x9A\x84", {"de5", "de5"}}, {"\xE9\x87\x8D", {"zhong4", "chong2"}},
    };
    static const std::unordered_map<std::string, const char*> kana = {
        {"\xE3\x81\x82", "a"}, {"\xE3\x81\x84", "i"}, {"\xE3\x81\x86", "u"},
        {"\xE3\x81\x88", "e"}, {"\xE3\x81\x8A", "o"}, {"\xE3\x81\x8B", "ka"},
        {"\xE3\x81\x8D", "ki"}, {"\xE3\x81\x8F", "ku"}, {"\xE3\x81\x91", "ke"},
        {"\xE3\x81\x93", "ko"}, {"\xE3\x82\x93", "n"}, {"\xE3\x81\xA3", "tt"},
        {"\xE3\x82\xAD\xE3\x83\xA3", "kya"}, {"\xE3\x81\x8D\xE3\x82\x83", "kya"},
    };
    if (const auto it = pinyin.find(lyric); it != pinyin.end()) {
        std::vector<std::string> candidates{it->second.first};
        if (std::string_view(it->second.first) != it->second.second) candidates.emplace_back(it->second.second);
        return {it->second.first, std::move(candidates)};
    }
    if (const auto it = kana.find(lyric); it != kana.end()) return {it->second, {it->second}};
    return {{}, {}};
}

std::vector<double> distribute_lengths(const svs_phoneme* phonemes, size_t count, double space) {
    std::vector<double> lengths(count);
    if (count == 0) return lengths;
    double natural_total = 0;
    for (size_t index = 0; index < count; ++index) natural_total += (std::max)(0.0, phonemes[index].duration);
    const double factor = natural_total > 0 ? (std::max)(0.0, space) / natural_total : 0;
    for (size_t index = 0; index < count; ++index) lengths[index] = (std::max)(0.0, phonemes[index].duration) * factor;
    return lengths;
}

bool valid_string(svs_string_view value) {
    return value.data != nullptr;
}

std::string copy_string(svs_string_view value) {
    return std::string(value.data, value.size);
}

std::string manifest_value(const std::string& manifest, const char* key) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(manifest, match, pattern) ? match[1].str() : std::string();
}

svs_image make_image_view(const svs_context::image_data& image) {
    return {{image.mime_type.data(), image.mime_type.size()}, {image.path.data(), image.path.size()},
            image.data.empty() ? nullptr : image.data.data(), image.data.size()};
}

void refresh_engine_views(svs_context* context) {
    context->voice_source_view.clear();
    context->voice_source_view.reserve(context->voice_sources.size());
    for (const auto& source : context->voice_sources) {
        context->voice_source_view.push_back({string_view(source.id), string_view(source.name),
            string_view(source.description), make_image_view(source.avatar), make_image_view(source.portrait)});
    }
    context->format_view.clear();
    context->format_view.reserve(context->formats.size());
    for (const auto& format : context->formats) {
        context->format_view.push_back({string_view(format.plugin_id), string_view(format.name),
                                        string_view(format.extension)});
    }
}

svs_status load_native_plugin(svs_context* context, const std::filesystem::path& package,
                              const std::string& plugin_id, const std::string& library) {
    const auto path = package / library;
#if defined(_WIN32)
    HMODULE module = LoadLibraryW(path.c_str());
    auto entry = module == nullptr ? nullptr : reinterpret_cast<svs_plugin_get_api_fn>(
        GetProcAddress(module, "svs_plugin_get_api"));
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto entry = module == nullptr ? nullptr : reinterpret_cast<svs_plugin_get_api_fn>(
        dlsym(module, "svs_plugin_get_api"));
#endif
    uint32_t version = 0;
    const svs_plugin_vtable* plugin = entry == nullptr ? nullptr : entry(SVS_PLUGIN_API_VERSION, &version);
    if (module == nullptr || plugin == nullptr || version != SVS_PLUGIN_API_VERSION ||
        plugin->size < sizeof(svs_plugin_vtable)) {
        if (module != nullptr) close_library(module);
        set_error(context, "Native engine could not be loaded or has an incompatible plugin API.");
        return SVS_ERR_MODULE_LOAD;
    }
    for (size_t index = 0; index < plugin->voice_source_count(); ++index) {
        svs_voice_source_info info{};
        if (plugin->voice_source_get(index, &info) != SVS_OK || !valid_string(info.id) ||
            !valid_string(info.name) || !valid_string(info.description)) continue;
        auto copy_image = [](const svs_image& image) {
            svs_context::image_data result{copy_string(image.mime_type), copy_string(image.path)};
            if (image.data != nullptr && image.size > 0) result.data.assign(image.data, image.data + image.size);
            return result;
        };
        context->voice_sources.push_back({copy_string(info.id), copy_string(info.name),
                                          copy_string(info.description), copy_image(info.avatar),
                                          copy_image(info.portrait)});
    }
    for (size_t index = 0; index < plugin->format_count(); ++index) {
        const char* name = plugin->format_name(index);
        const char* extension = plugin->format_extension(index);
        if (name != nullptr && extension != nullptr) context->formats.push_back({plugin_id, name, extension});
    }
    context->modules.push_back(module);
    return SVS_OK;
}

#if defined(SVS_CORE_ENABLE_DOTNET_HOSTING) && defined(_WIN32)
using load_assembly_and_get_function_pointer_fn = int (CORECLR_DELEGATE_CALLTYPE *)(
    const char_t*, const char_t*, const char_t*, const char_t*, void*, void**);
using managed_plugin_entry_fn = const svs_plugin_vtable* (CORECLR_DELEGATE_CALLTYPE *)(uint32_t, uint32_t*);

svs_status load_dotnet_plugin(svs_context* context, const std::filesystem::path& package,
                              const std::string& plugin_id, const std::string& assembly,
                              const std::string& class_name) {
    const auto assembly_path = package / assembly;
    const auto runtime_config = assembly_path.parent_path() / (assembly_path.stem().wstring() + L".runtimeconfig.json");
    if (!std::filesystem::exists(assembly_path) || !std::filesystem::exists(runtime_config) || class_name.empty()) {
        set_error(context, "Managed engine assembly, runtimeconfig, or entry class is missing.");
        return SVS_ERR_MODULE_LOAD;
    }
    size_t hostfxr_path_size = MAX_PATH;
    std::vector<char_t> hostfxr_path(hostfxr_path_size);
    if (get_hostfxr_path(hostfxr_path.data(), &hostfxr_path_size, nullptr) != 0) {
        set_error(context, "nethost could not locate hostfxr.");
        return SVS_ERR_MODULE_LOAD;
    }
    HMODULE hostfxr = LoadLibraryW(hostfxr_path.data());
    auto initialize = hostfxr == nullptr ? nullptr : reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(hostfxr, "hostfxr_initialize_for_runtime_config"));
    auto get_delegate = hostfxr == nullptr ? nullptr : reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(hostfxr, "hostfxr_get_runtime_delegate"));
    auto close = hostfxr == nullptr ? nullptr : reinterpret_cast<hostfxr_close_fn>(GetProcAddress(hostfxr, "hostfxr_close"));
    hostfxr_handle host_context = nullptr;
    void* delegate = nullptr;
    if (initialize == nullptr || get_delegate == nullptr || close == nullptr ||
        initialize(runtime_config.c_str(), nullptr, &host_context) < 0 || host_context == nullptr ||
        get_delegate(host_context, hdt_load_assembly_and_get_function_pointer, &delegate) < 0 || delegate == nullptr) {
        if (host_context != nullptr && close != nullptr) close(host_context);
        if (hostfxr != nullptr) FreeLibrary(hostfxr);
        set_error(context, "hostfxr failed to initialize the managed engine runtime.");
        return SVS_ERR_MODULE_LOAD;
    }
    close(host_context);
    const std::wstring type_name = std::filesystem::path(class_name).wstring() + L", " + assembly_path.stem().wstring();
    void* entry_pointer = nullptr;
    const auto load_assembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegate);
    if (load_assembly(assembly_path.c_str(), type_name.c_str(), L"GetApi", UNMANAGEDCALLERSONLY_METHOD,
                      nullptr, &entry_pointer) != 0 || entry_pointer == nullptr) {
        FreeLibrary(hostfxr);
        set_error(context, "Managed engine entry point could not be resolved.");
        return SVS_ERR_MODULE_LOAD;
    }
    uint32_t version = 0;
    const auto entry = reinterpret_cast<managed_plugin_entry_fn>(entry_pointer);
    const svs_plugin_vtable* plugin = entry(SVS_PLUGIN_API_VERSION, &version);
    if (plugin == nullptr) {
        FreeLibrary(hostfxr);
        set_error(context, "Managed engine entry returned a null plugin API.");
        return SVS_ERR_MODULE_VERSION;
    }
    if (version != SVS_PLUGIN_API_VERSION) {
        FreeLibrary(hostfxr);
        context->last_error = "Managed engine API version mismatch: " + std::to_string(version);
        return SVS_ERR_MODULE_VERSION;
    }
    if (plugin->size < sizeof(svs_plugin_vtable)) {
        FreeLibrary(hostfxr);
        context->last_error = "Managed engine vtable is too small: " + std::to_string(plugin->size);
        return SVS_ERR_MODULE_VERSION;
    }
    for (size_t index = 0; index < plugin->voice_source_count(); ++index) {
        svs_voice_source_info info{};
        if (plugin->voice_source_get(index, &info) != SVS_OK || !valid_string(info.id) ||
            !valid_string(info.name) || !valid_string(info.description)) continue;
        auto copy_image = [](const svs_image& image) {
            svs_context::image_data result{copy_string(image.mime_type), copy_string(image.path)};
            if (image.data != nullptr && image.size > 0) result.data.assign(image.data, image.data + image.size);
            return result;
        };
        context->voice_sources.push_back({copy_string(info.id), copy_string(info.name),
                                          copy_string(info.description), copy_image(info.avatar),
                                          copy_image(info.portrait)});
    }
    for (size_t index = 0; index < plugin->format_count(); ++index) {
        const char* name = plugin->format_name(index);
        const char* extension = plugin->format_extension(index);
        if (name != nullptr && extension != nullptr) context->formats.push_back({plugin_id, name, extension});
    }
    context->modules.push_back(hostfxr);
    return SVS_OK;
}
#endif

double evaluate_points(const std::vector<svs_pitch_point>& points, double tick) {
    if (points.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (tick <= points.front().tick) return points.front().pitch;
    if (tick >= points.back().tick) return points.back().pitch;
    const auto upper = std::upper_bound(points.begin(), points.end(), tick,
                                        [](double value, const svs_pitch_point& point) {
                                            return value < point.tick;
                                        });
    const auto& right = *upper;
    const auto& left = *std::prev(upper);
    const double fraction = (tick - left.tick) / (right.tick - left.tick);
    return left.pitch + (right.pitch - left.pitch) * fraction;
}

bool valid_points(const svs_pitch_point* points, size_t count) {
    if (count > 0 && points == nullptr) return false;
    for (size_t index = 0; index < count; ++index) {
        if (!std::isfinite(points[index].tick) || !std::isfinite(points[index].pitch)) return false;
    }
    return true;
}

void normalize_points(std::vector<svs_pitch_point>& points) {
    std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
        return left.tick < right.tick;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const auto& left, const auto& right) {
        return left.tick == right.tick;
    }), points.end());
}

void touch(svs_automation* automation) {
    ++automation->revision;
    touch(automation->parent);
}

} // namespace

void set_error(svs_context* context, const char* message) {
    if (context != nullptr) {
        context->last_error = message;
    }
}

void touch(svs_score* score) {
    ++score->revision;
    ++score->context->revision;
}

void touch(svs_track* track) {
    ++track->revision;
    touch(track->parent);
}

void touch(svs_part* part) {
    if (part->batching) {
        part->batch_dirty = true;
        return;
    }
    ++part->revision;
    touch(part->parent);
}

void touch_or_defer(svs_part* part) {
    touch(part);
}

void touch(svs_note* note) {
    ++note->revision;
    touch(note->parent);
}

SVS_API svs_status svs_context_create(svs_context** out_context) {
    if (out_context == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    *out_context = nullptr;
    auto context = std::make_unique<svs_context>();
#if defined(_WIN32)
    constexpr const char* modules[] = {"svs_core_g2p.dll", "svs_core_layout.dll"};
#elif defined(__APPLE__)
    constexpr const char* modules[] = {"libsvs_core_g2p.dylib", "libsvs_core_layout.dylib"};
#else
    constexpr const char* modules[] = {"libsvs_core_g2p.so", "libsvs_core_layout.so"};
#endif
    for (const char* module : modules) {
        const svs_status status = load_module(context.get(), module);
        if (status != SVS_OK) {
            close_modules(context.get());
            return status;
        }
    }
    *out_context = context.release();
    return SVS_OK;
}

SVS_API void svs_context_destroy(svs_context* context) {
    if (context != nullptr) {
        close_modules(context);
        delete context;
    }
}

SVS_API const char* svs_last_error_message(const svs_context* context) {
    return context == nullptr ? "Invalid SVS context." : context->last_error.c_str();
}

SVS_API uint64_t svs_context_revision(const svs_context* context) {
    return context == nullptr ? 0 : context->revision;
}

SVS_API svs_status svs_context_set_engines_dir(svs_context* context, const char* path) {
    if (context == nullptr || path == nullptr || *path == '\0') return SVS_ERR_INVALID_ARG;
    context->engines_directory = path;
    return SVS_OK;
}

SVS_API svs_status svs_context_load_engines(svs_context* context) {
    if (context == nullptr) return SVS_ERR_INVALID_ARG;
    const std::filesystem::path engines = context->engines_directory.empty()
        ? core_directory() / "Engines" : std::filesystem::path(context->engines_directory);
    if (!std::filesystem::exists(engines)) return SVS_OK;
    svs_status first_error = SVS_OK;
    for (const auto& entry : std::filesystem::directory_iterator(engines)) {
        if (!entry.is_directory()) continue;
        const auto manifest_path = entry.path() / "manifest.json";
        std::ifstream input(manifest_path);
        if (!input) continue;
        const std::string manifest((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const std::string id = manifest_value(manifest, "id");
        const std::string sdk_version = manifest_value(manifest, "sdk-version");
        const std::string library = manifest_value(manifest, "library");
        const std::string runtime = manifest_value(manifest, "runtime");
        const std::string assembly = manifest_value(manifest, "assembly");
        const std::string class_name = manifest_value(manifest, "class");
        if (id.empty() || sdk_version != SVS_SDK_VERSION) {
            if (first_error == SVS_OK) first_error = SVS_ERR_MODULE_VERSION;
            continue;
        }
        svs_status status = SVS_OK;
        if (!library.empty()) {
            status = load_native_plugin(context, entry.path(), id, library);
        } else if (runtime == "dotnet") {
#if defined(SVS_CORE_ENABLE_DOTNET_HOSTING) && defined(_WIN32)
            status = load_dotnet_plugin(context, entry.path(), id, assembly, class_name);
#else
            set_error(context, "Managed engine found but SVS Core was built without .NET hosting.");
            status = SVS_ERR_MODULE_LOAD;
#endif
        } else {
            continue;
        }
        if (status != SVS_OK && first_error == SVS_OK) first_error = status;
    }
    refresh_engine_views(context);
    return first_error;
}

SVS_API svs_status svs_context_get_voice_sources(svs_context* context,
                                                  const svs_voice_source_info** out_sources,
                                                  size_t* out_count) {
    if (context == nullptr || out_sources == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    *out_sources = context->voice_source_view.data();
    *out_count = context->voice_source_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_context_get_formats(svs_context* context, const svs_format_info** out_formats,
                                           size_t* out_count) {
    if (context == nullptr || out_formats == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    *out_formats = context->format_view.data();
    *out_count = context->format_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_score_create(svs_context* context, svs_score** out_score) {
    if (context == nullptr || out_score == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    *out_score = new (std::nothrow) svs_score{context};
    return *out_score == nullptr ? SVS_ERR_INTERNAL : SVS_OK;
}

SVS_API void svs_score_destroy(svs_score* score) {
    delete score;
}

SVS_API uint64_t svs_score_revision(const svs_score* score) {
    return score == nullptr ? 0 : score->revision;
}

SVS_API svs_status svs_score_create_track(svs_score* score, svs_track** out_track) {
    if (score == nullptr || out_track == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    auto track = std::make_unique<svs_track>(svs_track{score});
    *out_track = track.get();
    score->tracks.push_back(std::move(track));
    touch(score);
    return SVS_OK;
}

SVS_API svs_status svs_tempo_set_point(svs_score* score, double tick, double bpm) {
    if (score == nullptr || !std::isfinite(tick) || !std::isfinite(bpm) || tick < 0 || bpm <= 0) {
        return SVS_ERR_INVALID_ARG;
    }
    auto it = std::lower_bound(score->tempos.begin(), score->tempos.end(), tick,
                               [](const svs_tempo_point& point, double value) {
                                   return point.tick < value;
                               });
    if (it != score->tempos.end() && it->tick == tick) {
        it->bpm = bpm;
    } else {
        score->tempos.insert(it, {tick, bpm});
    }
    touch(score);
    return SVS_OK;
}

SVS_API svs_status svs_tempo_get_points(const svs_score* score,
                                        const svs_tempo_point** out_points, size_t* out_count) {
    if (score == nullptr || out_points == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    *out_points = score->tempos.data();
    *out_count = score->tempos.size();
    return SVS_OK;
}

SVS_API double svs_tempo_bpm_at(const svs_score* score, double tick) {
    if (score == nullptr) return 0;
    const auto it = std::upper_bound(score->tempos.begin(), score->tempos.end(), tick,
                                     [](double value, const svs_tempo_point& point) {
                                         return value < point.tick;
                                     });
    return (it == score->tempos.begin() ? it : std::prev(it))->bpm;
}

SVS_API svs_status svs_time_signature_set(svs_score* score, int32_t bar, int32_t numerator,
                                          int32_t denominator) {
    if (score == nullptr || bar < 0 || numerator <= 0 || denominator <= 0 ||
        (denominator & (denominator - 1)) != 0) return SVS_ERR_INVALID_ARG;
    auto it = std::lower_bound(score->time_signatures.begin(), score->time_signatures.end(), bar,
                               [](const svs_time_signature& signature, int32_t value) {
                                   return signature.bar < value;
                               });
    if (it != score->time_signatures.end() && it->bar == bar) {
        it->numerator = numerator;
        it->denominator = denominator;
    } else {
        score->time_signatures.insert(it, {bar, numerator, denominator});
    }
    touch(score);
    return SVS_OK;
}

SVS_API svs_status svs_time_signature_get(const svs_score* score,
                                          const svs_time_signature** out_signatures,
                                          size_t* out_count) {
    if (score == nullptr || out_signatures == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    *out_signatures = score->time_signatures.data();
    *out_count = score->time_signatures.size();
    return SVS_OK;
}

SVS_API double svs_score_tick_to_seconds(const svs_score* score, double tick) {
    if (score == nullptr) return 0;
    const auto it = std::upper_bound(score->tempos.begin(), score->tempos.end(), tick,
                                     [](double value, const svs_tempo_point& point) {
                                         return value < point.tick;
                                     });
    const size_t index = it == score->tempos.begin() ? 0 :
        static_cast<size_t>(std::distance(score->tempos.begin(), std::prev(it)));
    const auto& point = score->tempos[index];
    return seconds_at_tempo_index(score, index) +
           (tick - point.tick) * 60.0 / (point.bpm * SVS_PPQ);
}

SVS_API double svs_score_seconds_to_tick(const svs_score* score, double seconds) {
    if (score == nullptr) return 0;
    size_t index = 0;
    for (size_t next = 1; next < score->tempos.size(); ++next) {
        if (seconds_at_tempo_index(score, next) > seconds) break;
        index = next;
    }
    const auto& point = score->tempos[index];
    return point.tick + (seconds - seconds_at_tempo_index(score, index)) * point.bpm * SVS_PPQ / 60.0;
}

SVS_API double svs_score_tick_to_beat(const svs_score* score, double tick) {
    if (score == nullptr) return 0;
    const meter_position meter = meter_at_tick(score, tick);
    return meter.beat + (tick - meter.tick) / ticks_per_beat(*meter.signature);
}

SVS_API double svs_score_beat_to_tick(const svs_score* score, double beat) {
    if (score == nullptr) return 0;
    if (beat < 0) return beat * ticks_per_beat(score->time_signatures.front());
    meter_position meter = meter_at_bar(score, 0);
    for (size_t index = 1; index < score->time_signatures.size(); ++index) {
        const meter_position candidate = meter_at_bar(score, score->time_signatures[index].bar);
        if (candidate.beat > beat) break;
        meter = candidate;
    }
    return meter.tick + (beat - meter.beat) * ticks_per_beat(*meter.signature);
}

SVS_API svs_status svs_score_tick_to_bar_beat(const svs_score* score, double tick,
                                               svs_bar_beat* out_position) {
    if (score == nullptr || out_position == nullptr) return SVS_ERR_INVALID_ARG;
    const auto& first = score->time_signatures.front();
    if (tick < 0) {
        *out_position = {0, tick / ticks_per_beat(first)};
        return SVS_OK;
    }
    const meter_position meter = meter_at_tick(score, tick);
    const double beats = (tick - meter.tick) / ticks_per_beat(*meter.signature);
    *out_position = {meter.signature->bar + static_cast<int32_t>(std::floor(beats / meter.signature->numerator)),
                     std::fmod(beats, static_cast<double>(meter.signature->numerator))};
    return SVS_OK;
}

SVS_API double svs_score_bar_to_tick(const svs_score* score, int32_t bar) {
    if (score == nullptr) return 0;
    const auto& first = score->time_signatures.front();
    if (bar < 0) return bar * ticks_per_bar(first);
    const meter_position meter = meter_at_bar(score, bar);
    return meter.tick + (bar - meter.signature->bar) * ticks_per_bar(*meter.signature);
}

SVS_API svs_status svs_score_get_info(const svs_score* score, svs_score_info* out_info) {
    if (score == nullptr || out_info == nullptr) return SVS_ERR_INVALID_ARG;
    const double tick_count = score_end_tick(score);
    svs_bar_beat position{};
    svs_score_tick_to_bar_beat(score, tick_count, &position);
    *out_info = {SVS_PPQ, svs_tempo_bpm_at(score, tick_count), score->tempos.size(),
                 score->time_signatures.size(), tick_count, svs_score_tick_to_seconds(score, tick_count),
                 position.bar + 1, score->revision};
    return SVS_OK;
}

SVS_API svs_status svs_track_create_part(svs_track* track, svs_part** out_part) {
    if (track == nullptr || out_part == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    auto part = std::make_unique<svs_part>(svs_part{track});
    *out_part = part.get();
    track->parts.push_back(std::move(part));
    touch(track);
    return SVS_OK;
}

SVS_API svs_status svs_part_create_note(svs_part* part, double pos, double dur, int32_t pitch,
                                         const char* lyric, svs_note** out_note) {
    if (part == nullptr || lyric == nullptr || out_note == nullptr || !std::isfinite(pos) ||
        !std::isfinite(dur) || dur < 0) {
        return SVS_ERR_INVALID_ARG;
    }
    auto note = std::make_unique<svs_note>(svs_note{part, pos, dur, pitch, lyric});
    *out_note = note.get();
    part->notes.push_back(std::move(note));
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_note_remove(svs_part* part, svs_note* note) {
    if (part == nullptr || note == nullptr || note->parent != part) return SVS_ERR_INVALID_ARG;
    const auto it = std::find_if(part->notes.begin(), part->notes.end(), [note](const auto& item) {
        return item.get() == note;
    });
    if (it == part->notes.end()) return SVS_ERR_NOT_FOUND;
    part->notes.erase(it);
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_get_notes(svs_part* part, const svs_note* const** out_notes, size_t* out_count) {
    if (part == nullptr || out_notes == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    refresh_note_view(part);
    *out_notes = reinterpret_cast<const svs_note* const*>(part->note_view.data());
    *out_count = part->note_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_part_move_notes(svs_part* part, svs_note* const* notes, size_t count,
                                       double delta_tick, int32_t delta_pitch) {
    if (part == nullptr || (count > 0 && notes == nullptr) || !std::isfinite(delta_tick)) {
        return SVS_ERR_INVALID_ARG;
    }
    const bool was_batching = part->batching;
    if (!was_batching) svs_part_begin_batch(part);
    for (size_t index = 0; index < count; ++index) {
        if (notes[index] == nullptr || notes[index]->parent != part) {
            if (!was_batching) svs_part_end_batch(part);
            return SVS_ERR_INVALID_ARG;
        }
        notes[index]->pos += delta_tick;
        notes[index]->pitch += delta_pitch;
        ++notes[index]->revision;
        part->batch_dirty = true;
    }
    if (!was_batching) return svs_part_end_batch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_begin_batch(svs_part* part) {
    if (part == nullptr || part->batching) return SVS_ERR_INVALID_ARG;
    part->batching = true;
    part->batch_dirty = false;
    return SVS_OK;
}

SVS_API svs_status svs_part_end_batch(svs_part* part) {
    if (part == nullptr || !part->batching) return SVS_ERR_INVALID_ARG;
    part->batching = false;
    if (part->batch_dirty) {
        part->batch_dirty = false;
        ++part->revision;
        touch(part->parent);
    }
    return SVS_OK;
}

SVS_API svs_status svs_part_pitch_set_segments(svs_part* part,
                                                const svs_pitch_segment* segments, size_t count) {
    if (part == nullptr || (count > 0 && segments == nullptr)) return SVS_ERR_INVALID_ARG;
    std::vector<pitch_segment_data> copied;
    copied.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (segments[index].count > 0 && segments[index].points == nullptr) return SVS_ERR_INVALID_ARG;
        pitch_segment_data segment;
        segment.points.assign(segments[index].points, segments[index].points + segments[index].count);
        for (const auto& point : segment.points) {
            if (!std::isfinite(point.tick) || !std::isfinite(point.pitch)) return SVS_ERR_INVALID_ARG;
        }
        std::sort(segment.points.begin(), segment.points.end(), [](const auto& left, const auto& right) {
            return left.tick < right.tick;
        });
        copied.push_back(std::move(segment));
    }
    part->pitch_segments = std::move(copied);
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_pitch_get_segments(svs_part* part,
                                                const svs_pitch_segment** out_segments,
                                                size_t* out_count) {
    if (part == nullptr || out_segments == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    part->pitch_view.clear();
    part->pitch_view.reserve(part->pitch_segments.size());
    for (const auto& segment : part->pitch_segments) {
        part->pitch_view.push_back({segment.points.data(), segment.points.size()});
    }
    *out_segments = part->pitch_view.data();
    *out_count = part->pitch_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_part_pitch_add_point(svs_part* part, size_t segment_index,
                                            double tick, double pitch) {
    if (part == nullptr || segment_index >= part->pitch_segments.size() || !std::isfinite(tick) ||
        !std::isfinite(pitch)) return SVS_ERR_INVALID_ARG;
    auto& points = part->pitch_segments[segment_index].points;
    points.push_back({tick, pitch});
    std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) { return left.tick < right.tick; });
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_set_automation_configs(svs_part* part,
                                                    const svs_automation_config* configs,
                                                    size_t count) {
    if (part == nullptr || (count > 0 && configs == nullptr)) return SVS_ERR_INVALID_ARG;
    std::vector<automation_config_data> copied;
    copied.reserve(count);
    std::unordered_map<std::string, bool> seen;
    for (size_t index = 0; index < count; ++index) {
        const auto& config = configs[index];
        if (!valid_string(config.id) || !valid_string(config.display_name) || !valid_string(config.color) ||
            config.id.size == 0 || !std::isfinite(config.min_value) || !std::isfinite(config.max_value) ||
            config.min_value > config.max_value ||
            (config.shape == SVS_AUTOMATION_CONTINUOUS && !std::isfinite(config.default_value)) ||
            (config.shape != SVS_AUTOMATION_CONTINUOUS && config.shape != SVS_AUTOMATION_PIECEWISE)) {
            return SVS_ERR_INVALID_ARG;
        }
        const std::string id = copy_string(config.id);
        if (!seen.emplace(id, true).second) return SVS_ERR_INVALID_ARG;
        copied.push_back({id, copy_string(config.display_name), config.min_value, config.max_value,
                          config.default_value, copy_string(config.color), config.shape});
    }
    std::unordered_map<std::string, std::unique_ptr<svs_automation>> next_automations;
    for (const auto& config : copied) {
        auto existing = part->automations.find(config.id);
        if (existing != part->automations.end() && existing->second->shape == config.shape) {
            existing->second->default_value = config.default_value;
            next_automations.emplace(config.id, std::move(existing->second));
        } else {
            auto automation = std::make_unique<svs_automation>();
            automation->parent = part;
            automation->id = config.id;
            automation->shape = config.shape;
            automation->default_value = config.default_value;
            next_automations.emplace(config.id, std::move(automation));
        }
    }
    part->automation_configs = std::move(copied);
    part->automations = std::move(next_automations);
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_get_automation_configs(svs_part* part,
                                                    const svs_automation_config** out_configs,
                                                    size_t* out_count) {
    if (part == nullptr || out_configs == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    part->automation_config_view.clear();
    part->automation_config_view.reserve(part->automation_configs.size());
    for (const auto& config : part->automation_configs) {
        part->automation_config_view.push_back({string_view(config.id), string_view(config.display_name),
                                                config.min_value, config.max_value, config.default_value,
                                                string_view(config.color), config.shape});
    }
    *out_configs = part->automation_config_view.data();
    *out_count = part->automation_config_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_part_get_automation(svs_part* part, const char* id,
                                            svs_automation** out_automation) {
    if (part == nullptr || id == nullptr || out_automation == nullptr) return SVS_ERR_INVALID_ARG;
    const auto it = part->automations.find(id);
    if (it == part->automations.end()) return SVS_ERR_NOT_FOUND;
    *out_automation = it->second.get();
    return SVS_OK;
}

SVS_API svs_status svs_automation_set_default_value(svs_automation* automation, double value) {
    if (automation == nullptr || automation->shape != SVS_AUTOMATION_CONTINUOUS || !std::isfinite(value)) {
        return SVS_ERR_INVALID_ARG;
    }
    automation->default_value = value;
    touch(automation);
    return SVS_OK;
}

SVS_API double svs_automation_default_value(const svs_automation* automation) {
    if (automation == nullptr || automation->shape == SVS_AUTOMATION_PIECEWISE) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return automation->default_value;
}

SVS_API svs_status svs_automation_set_points(svs_automation* automation,
                                             const svs_pitch_point* points, size_t count) {
    if (automation == nullptr || automation->shape != SVS_AUTOMATION_CONTINUOUS || !valid_points(points, count)) {
        return SVS_ERR_INVALID_ARG;
    }
    automation->points.assign(points, points + count);
    normalize_points(automation->points);
    touch(automation);
    return SVS_OK;
}

SVS_API svs_status svs_automation_get_points(const svs_automation* automation,
                                             const svs_pitch_point** out_points, size_t* out_count) {
    if (automation == nullptr || out_points == nullptr || out_count == nullptr ||
        automation->shape != SVS_AUTOMATION_CONTINUOUS) return SVS_ERR_INVALID_ARG;
    *out_points = automation->points.data();
    *out_count = automation->points.size();
    return SVS_OK;
}

SVS_API svs_status svs_automation_set_segments(svs_automation* automation,
                                               const svs_pitch_segment* segments, size_t count) {
    if (automation == nullptr || automation->shape != SVS_AUTOMATION_PIECEWISE ||
        (count > 0 && segments == nullptr)) return SVS_ERR_INVALID_ARG;
    std::vector<pitch_segment_data> copied;
    copied.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (!valid_points(segments[index].points, segments[index].count)) return SVS_ERR_INVALID_ARG;
        pitch_segment_data segment;
        segment.points.assign(segments[index].points, segments[index].points + segments[index].count);
        normalize_points(segment.points);
        if (!segment.points.empty()) copied.push_back(std::move(segment));
    }
    std::sort(copied.begin(), copied.end(), [](const auto& left, const auto& right) {
        return left.points.front().tick < right.points.front().tick;
    });
    automation->segments = std::move(copied);
    touch(automation);
    return SVS_OK;
}

SVS_API svs_status svs_automation_get_segments(svs_automation* automation,
                                               const svs_pitch_segment** out_segments,
                                               size_t* out_count) {
    if (automation == nullptr || out_segments == nullptr || out_count == nullptr ||
        automation->shape != SVS_AUTOMATION_PIECEWISE) return SVS_ERR_INVALID_ARG;
    automation->segment_view.clear();
    automation->segment_view.reserve(automation->segments.size());
    for (const auto& segment : automation->segments) {
        automation->segment_view.push_back({segment.points.data(), segment.points.size()});
    }
    *out_segments = automation->segment_view.data();
    *out_count = automation->segment_view.size();
    return SVS_OK;
}

SVS_API double svs_automation_evaluate(const svs_automation* automation, double tick) {
    if (automation == nullptr || !std::isfinite(tick)) return std::numeric_limits<double>::quiet_NaN();
    if (automation->shape == SVS_AUTOMATION_CONTINUOUS) {
        const double deviation = evaluate_points(automation->points, tick);
        return std::isnan(deviation) ? automation->default_value : automation->default_value + deviation;
    }
    for (const auto& segment : automation->segments) {
        if (tick >= segment.points.front().tick && tick <= segment.points.back().tick) {
            return evaluate_points(segment.points, tick);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

SVS_API svs_status svs_part_set_note_property_configs(svs_part* part,
                                                       const svs_property_config* configs,
                                                       size_t count) {
    if (part == nullptr || (count > 0 && configs == nullptr)) return SVS_ERR_INVALID_ARG;
    std::vector<property_config_data> copied;
    copied.reserve(count);
    std::unordered_map<std::string, bool> seen;
    for (size_t index = 0; index < count; ++index) {
        const auto& config = configs[index];
        if (!valid_string(config.id) || !valid_string(config.display_name) || !valid_string(config.default_text) ||
            config.id.size == 0 || !std::isfinite(config.default_number) ||
            (config.kind != SVS_PROPERTY_NUMBER && config.kind != SVS_PROPERTY_TEXT)) return SVS_ERR_INVALID_ARG;
        const std::string id = copy_string(config.id);
        if (!seen.emplace(id, true).second) return SVS_ERR_INVALID_ARG;
        copied.push_back({id, copy_string(config.display_name), config.kind,
                          config.default_number, copy_string(config.default_text)});
    }
    part->note_property_configs = std::move(copied);
    touch(part);
    return SVS_OK;
}

SVS_API svs_status svs_part_get_note_property_configs(svs_part* part,
                                                       const svs_property_config** out_configs,
                                                       size_t* out_count) {
    if (part == nullptr || out_configs == nullptr || out_count == nullptr) return SVS_ERR_INVALID_ARG;
    part->note_property_config_view.clear();
    part->note_property_config_view.reserve(part->note_property_configs.size());
    for (const auto& config : part->note_property_configs) {
        part->note_property_config_view.push_back({string_view(config.id), string_view(config.display_name), config.kind,
                                                   config.default_number, string_view(config.default_text)});
    }
    *out_configs = part->note_property_config_view.data();
    *out_count = part->note_property_config_view.size();
    return SVS_OK;
}

SVS_API svs_status svs_note_get_info(const svs_note* note, svs_note_info* out_info) {
    if (note == nullptr || out_info == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    *out_info = {note->pos, note->dur, note->pitch, string_view(note->lyric),
                 string_view(note->pronunciation), note->revision};
    return SVS_OK;
}

SVS_API svs_status svs_note_set_pos(svs_note* note, double pos) {
    if (note == nullptr) return SVS_ERR_INVALID_ARG;
    note->pos = pos;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_set_dur(svs_note* note, double dur) {
    if (note == nullptr || dur < 0) return SVS_ERR_INVALID_ARG;
    note->dur = dur;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_set_pitch(svs_note* note, int32_t pitch) {
    if (note == nullptr) return SVS_ERR_INVALID_ARG;
    note->pitch = pitch;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_set_lyric(svs_note* note, const char* lyric) {
    if (note == nullptr || lyric == nullptr) return SVS_ERR_INVALID_ARG;
    note->lyric = lyric;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_set_pronunciation(svs_note* note, const char* pronunciation) {
    if (note == nullptr || pronunciation == nullptr) return SVS_ERR_INVALID_ARG;
    note->pronunciation = pronunciation;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_property_set_double(svs_note* note, const char* key, double value) {
    if (note == nullptr || key == nullptr) return SVS_ERR_INVALID_ARG;
    note->properties[key] = value;
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_property_get_double(const svs_note* note, const char* key,
                                                 double* out_value) {
    if (note == nullptr || key == nullptr || out_value == nullptr) return SVS_ERR_INVALID_ARG;
    const auto it = note->properties.find(key);
    if (it == note->properties.end() || !std::holds_alternative<double>(it->second)) return SVS_ERR_NOT_FOUND;
    *out_value = std::get<double>(it->second);
    return SVS_OK;
}

SVS_API svs_status svs_note_property_set_string(svs_note* note, const char* key, const char* value) {
    if (note == nullptr || key == nullptr || value == nullptr) return SVS_ERR_INVALID_ARG;
    note->properties[key] = std::string(value);
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_property_get_string(const svs_note* note, const char* key,
                                                 svs_string_view* out_value) {
    if (note == nullptr || key == nullptr || out_value == nullptr) return SVS_ERR_INVALID_ARG;
    const auto it = note->properties.find(key);
    if (it == note->properties.end() || !std::holds_alternative<std::string>(it->second)) return SVS_ERR_NOT_FOUND;
    *out_value = string_view(std::get<std::string>(it->second));
    return SVS_OK;
}

SVS_API svs_status svs_note_get_phonemes(svs_note* note, svs_phoneme_list* out_phonemes) {
    if (note == nullptr || out_phonemes == nullptr) return SVS_ERR_INVALID_ARG;
    auto& scratch = context_for(note->parent)->scratch_phonemes;
    scratch.clear();
    scratch.reserve(note->leading_phonemes.size() + note->body_phonemes.size());
    append_public_phonemes(scratch, note->leading_phonemes);
    append_public_phonemes(scratch, note->body_phonemes);
    *out_phonemes = {scratch.data(), scratch.size(), note->leading_phonemes.size()};
    return SVS_OK;
}

SVS_API svs_status svs_note_phoneme_set(svs_note* note, int32_t slot, const svs_phoneme* phoneme) {
    if (note == nullptr || !valid_phoneme(phoneme)) return SVS_ERR_INVALID_ARG;
    const int32_t index = slot + static_cast<int32_t>(note->leading_phonemes.size());
    const size_t count = note->leading_phonemes.size() + note->body_phonemes.size();
    if (index < 0 || static_cast<size_t>(index) >= count) return SVS_ERR_NOT_FOUND;
    auto& target = static_cast<size_t>(index) < note->leading_phonemes.size()
        ? note->leading_phonemes[static_cast<size_t>(index)]
        : note->body_phonemes[static_cast<size_t>(index) - note->leading_phonemes.size()];
    target = copy_phoneme(*phoneme);
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_phoneme_insert(svs_note* note, int32_t slot, const svs_phoneme* phoneme) {
    if (note == nullptr || !valid_phoneme(phoneme)) return SVS_ERR_INVALID_ARG;
    const auto value = copy_phoneme(*phoneme);
    if (slot < 0) {
        const int32_t position = static_cast<int32_t>(note->leading_phonemes.size()) + slot + 1;
        if (position < 0 || position > static_cast<int32_t>(note->leading_phonemes.size())) return SVS_ERR_NOT_FOUND;
        note->leading_phonemes.insert(note->leading_phonemes.begin() + position, value);
    } else {
        if (static_cast<size_t>(slot) > note->body_phonemes.size()) return SVS_ERR_NOT_FOUND;
        note->body_phonemes.insert(note->body_phonemes.begin() + slot, value);
    }
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_phoneme_remove(svs_note* note, int32_t slot) {
    if (note == nullptr) return SVS_ERR_INVALID_ARG;
    const int32_t index = slot + static_cast<int32_t>(note->leading_phonemes.size());
    const size_t count = note->leading_phonemes.size() + note->body_phonemes.size();
    if (index < 0 || static_cast<size_t>(index) >= count) return SVS_ERR_NOT_FOUND;
    if (static_cast<size_t>(index) < note->leading_phonemes.size())
        note->leading_phonemes.erase(note->leading_phonemes.begin() + index);
    else
        note->body_phonemes.erase(note->body_phonemes.begin() + (index - static_cast<int32_t>(note->leading_phonemes.size())));
    touch(note);
    return SVS_OK;
}

SVS_API svs_status svs_note_set_body_offset(svs_note* note, double seconds) {
    if (note == nullptr || !std::isfinite(seconds)) return SVS_ERR_INVALID_ARG;
    note->body_offset = seconds;
    touch(note);
    return SVS_OK;
}

SVS_API double svs_note_body_offset(const svs_note* note) {
    return note == nullptr ? 0 : note->body_offset;
}

SVS_API svs_status svs_g2p_split_and_convert(svs_context* context, const char* text,
                                              const svs_lyric_result** out_results,
                                              size_t* out_count) {
    if (context == nullptr || text == nullptr || out_results == nullptr || out_count == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    const auto lyrics = split_lyrics(text);
    context->scratch_strings.clear();
    context->scratch_candidates.clear();
    context->scratch_lyrics.clear();
    context->scratch_strings.reserve(lyrics.size() * 4);
    context->scratch_candidates.reserve(lyrics.size() * 2);
    context->scratch_lyrics.reserve(lyrics.size());
    for (const auto& lyric : lyrics) {
        const auto [pronunciation, candidates] = pronunciation_for(lyric);
        context->scratch_strings.push_back(lyric);
        const size_t lyric_index = context->scratch_strings.size() - 1;
        context->scratch_strings.push_back(pronunciation);
        const size_t pronunciation_index = context->scratch_strings.size() - 1;
        const size_t candidate_start = context->scratch_candidates.size();
        for (const auto& candidate : candidates) {
            context->scratch_strings.push_back(candidate);
            context->scratch_candidates.push_back(string_view(context->scratch_strings.back()));
        }
        context->scratch_lyrics.push_back({string_view(context->scratch_strings[lyric_index]),
                                           string_view(context->scratch_strings[pronunciation_index]),
                                           context->scratch_candidates.data() + candidate_start,
                                           candidates.size()});
    }
    *out_results = context->scratch_lyrics.data();
    *out_count = context->scratch_lyrics.size();
    return SVS_OK;
}

SVS_API svs_status svs_g2p_predict_syllable(svs_context* context, const char* pronunciation,
                                            svs_phoneme_list* out_phonemes) {
    if (context == nullptr || pronunciation == nullptr || out_phonemes == nullptr) return SVS_ERR_INVALID_ARG;
    context->scratch_strings.clear();
    context->scratch_phonemes.clear();
    context->scratch_strings.reserve(2);
    const std::string value(pronunciation);
    if (value.empty()) {
        *out_phonemes = {nullptr, 0, 0};
        return SVS_OK;
    }
    const std::string vowels = "aeiou";
    const size_t vowel = value.find_first_of(vowels);
    if (vowel != std::string::npos && vowel > 0) {
        context->scratch_strings.push_back(value.substr(0, vowel));
        context->scratch_phonemes.push_back({string_view(context->scratch_strings.back()), 0.08, 0});
    }
    context->scratch_strings.push_back(value.substr(vowel == std::string::npos ? 0 : vowel));
    context->scratch_phonemes.push_back({string_view(context->scratch_strings.back()), 0.16, 1});
    const size_t leading_count = vowel != std::string::npos && vowel > 0 ? 1 : 0;
    *out_phonemes = {context->scratch_phonemes.data(), context->scratch_phonemes.size(), leading_count};
    return SVS_OK;
}

SVS_API svs_status svs_part_apply_lyrics_batch(svs_part* part, size_t from_note_index, const char* text) {
    if (part == nullptr || text == nullptr) return SVS_ERR_INVALID_ARG;
    refresh_note_view(part);
    if (from_note_index > part->note_view.size()) return SVS_ERR_INVALID_ARG;
    const auto lyrics = split_lyrics(text);
    const size_t available = part->note_view.size() - from_note_index;
    const size_t applied = (std::min)(available, lyrics.size());
    if (applied == 0) return SVS_OK;
    const bool was_batching = part->batching;
    if (!was_batching) svs_part_begin_batch(part);
    for (size_t index = 0; index < applied; ++index) {
        svs_note* note = part->note_view[from_note_index + index];
        note->lyric = lyrics[index];
        note->pronunciation = pronunciation_for(lyrics[index]).first;
        ++note->revision;
        part->batch_dirty = true;
    }
    return was_batching ? SVS_OK : svs_part_end_batch(part);
}

SVS_API svs_status svs_phoneme_layout_resolve(svs_context* context,
                                               const svs_phoneme_layout_note* notes,
                                               size_t note_count,
                                               const svs_phoneme_timing** out_timings,
                                               size_t* out_count) {
    if (context == nullptr || (note_count > 0 && notes == nullptr) || out_timings == nullptr || out_count == nullptr) {
        return SVS_ERR_INVALID_ARG;
    }
    auto& timings = context->scratch_timings;
    timings.clear();
    for (size_t index = 0; index < note_count; ++index) {
        const auto& note = notes[index];
        if (!std::isfinite(note.fill_start) || !std::isfinite(note.fill_end) ||
            !std::isfinite(note.body_offset) || note.fill_end < note.fill_start ||
            (note.leading_count > 0 && note.leading == nullptr) || (note.body_count > 0 && note.body == nullptr)) {
            return SVS_ERR_INVALID_ARG;
        }
        const double junction = note.fill_start + note.body_offset;
        std::vector<svs_phoneme_timing> leading(note.leading_count);
        double cursor = junction;
        for (size_t offset = note.leading_count; offset > 0; --offset) {
            const auto& phoneme = note.leading[offset - 1];
            const double duration = (std::max)(0.0, phoneme.duration);
            leading[offset - 1] = {cursor - duration, cursor};
            cursor -= duration;
        }
        timings.insert(timings.end(), leading.begin(), leading.end());
        const auto lengths = distribute_lengths(note.body, note.body_count, note.fill_end - note.fill_start);
        cursor = note.fill_start;
        for (size_t phoneme = 0; phoneme < note.body_count; ++phoneme) {
            timings.push_back({cursor, cursor + lengths[phoneme]});
            cursor += lengths[phoneme];
        }
    }
    *out_timings = timings.data();
    *out_count = timings.size();
    return SVS_OK;
}

SVS_API uint64_t svs_note_revision(const svs_note* note) {
    return note == nullptr ? 0 : note->revision;
}

SVS_API const svs_core_api* svs_core_get_api(void) {
    static const svs_core_api api = {
        sizeof(svs_core_api), SVS_CORE_ABI_VERSION, svs_context_create, svs_context_destroy,
        svs_last_error_message, svs_score_create, svs_score_destroy, svs_score_create_track,
        svs_track_create_part, svs_part_create_note, svs_tempo_set_point, svs_time_signature_set};
    return &api;
}