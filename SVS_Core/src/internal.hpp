#pragma once

#include "svs_core.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

constexpr uint32_t kModuleAbiVersion = 0x00010000u;

struct svs_context {
    uint64_t revision = 0;
    std::string last_error;
    std::vector<void*> modules;
    std::vector<std::string> scratch_strings;
    std::vector<svs_string_view> scratch_candidates;
    std::vector<svs_lyric_result> scratch_lyrics;
    std::vector<svs_phoneme> scratch_phonemes;
    std::vector<svs_phoneme_timing> scratch_timings;
    std::string engines_directory;
    struct image_data {
        std::string mime_type;
        std::string path;
        std::vector<unsigned char> data;
    };
    struct voice_source_data {
        std::string id;
        std::string name;
        std::string description;
        image_data avatar;
        image_data portrait;
    };
    struct format_data {
        std::string plugin_id;
        std::string name;
        std::string extension;
    };
    std::vector<voice_source_data> voice_sources;
    std::vector<format_data> formats;
    std::vector<svs_voice_source_info> voice_source_view;
    std::vector<svs_format_info> format_view;
};

struct phoneme_data {
    std::string symbol;
    double duration = 0;
    double stretch_weight = 1;
};

struct pitch_segment_data {
    std::vector<svs_pitch_point> points;
};

struct automation_config_data {
    std::string id;
    std::string display_name;
    double min_value = 0;
    double max_value = 1;
    double default_value = 0;
    std::string color;
    svs_automation_shape shape = SVS_AUTOMATION_CONTINUOUS;
};

struct property_config_data {
    std::string id;
    std::string display_name;
    svs_property_kind kind = SVS_PROPERTY_NUMBER;
    double default_number = 0;
    std::string default_text;
};

struct svs_score;
struct svs_track;
struct svs_part;

struct svs_note {
    svs_part* parent;
    double pos;
    double dur;
    int32_t pitch;
    std::string lyric;
    std::string pronunciation;
    std::unordered_map<std::string, std::variant<double, std::string>> properties;
    std::vector<phoneme_data> leading_phonemes;
    std::vector<phoneme_data> body_phonemes;
    double body_offset = 0;
    uint64_t revision = 0;
};

struct svs_part {
    svs_track* parent;
    std::vector<std::unique_ptr<svs_note>> notes;
    std::vector<pitch_segment_data> pitch_segments;
    std::unordered_map<std::string, std::unique_ptr<svs_automation>> automations;
    std::vector<automation_config_data> automation_configs;
    std::vector<svs_automation_config> automation_config_view;
    std::vector<property_config_data> note_property_configs;
    std::vector<svs_property_config> note_property_config_view;
    std::vector<svs_note*> note_view;
    std::vector<svs_pitch_segment> pitch_view;
    bool batching = false;
    bool batch_dirty = false;
    uint64_t revision = 0;
};

struct svs_automation {
    svs_part* parent;
    std::string id;
    svs_automation_shape shape = SVS_AUTOMATION_CONTINUOUS;
    double default_value = 0;
    std::vector<svs_pitch_point> points;
    std::vector<pitch_segment_data> segments;
    std::vector<svs_pitch_segment> segment_view;
    uint64_t revision = 0;
};

struct svs_track {
    svs_score* parent;
    std::vector<std::unique_ptr<svs_part>> parts;
    uint64_t revision = 0;
};

struct svs_score {
    svs_context* context;
    std::vector<std::unique_ptr<svs_track>> tracks;
    std::vector<svs_tempo_point> tempos{{0, 120}};
    std::vector<svs_time_signature> time_signatures{{0, 4, 4}};
    uint64_t revision = 0;
};

void touch(svs_note* note);
void touch(svs_part* part);
void touch(svs_track* track);
void touch(svs_score* score);
void set_error(svs_context* context, const char* message);
void touch_or_defer(svs_part* part);