#ifndef SVS_CORE_H
#define SVS_CORE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(SVS_CORE_EXPORTS)
#define SVS_API __declspec(dllexport)
#else
#define SVS_API __declspec(dllimport)
#endif
#else
#define SVS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SVS_CORE_ABI_VERSION 0x00010004u
#define SVS_SDK_VERSION "1.0"
#define SVS_PPQ 480

typedef struct svs_context svs_context;
typedef struct svs_score svs_score;
typedef struct svs_track svs_track;
typedef struct svs_part svs_part;
typedef struct svs_note svs_note;
typedef struct svs_automation svs_automation;

typedef enum svs_status {
    SVS_OK = 0,
    SVS_ERR_INVALID_ARG = 1,
    SVS_ERR_NOT_FOUND = 2,
    SVS_ERR_MODULE_LOAD = 3,
    SVS_ERR_MODULE_VERSION = 4,
    SVS_ERR_INTERNAL = 5
} svs_status;

typedef struct svs_string_view {
    const char* data;
    size_t size;
} svs_string_view;

typedef struct svs_note_info {
    double pos;
    double dur;
    int32_t pitch;
    svs_string_view lyric;
    svs_string_view pronunciation;
    uint64_t revision;
} svs_note_info;

typedef struct svs_tempo_point {
    double tick;
    double bpm;
} svs_tempo_point;

typedef struct svs_time_signature {
    int32_t bar;
    int32_t numerator;
    int32_t denominator;
} svs_time_signature;

typedef struct svs_bar_beat {
    int32_t bar;
    double beat;
} svs_bar_beat;

typedef struct svs_score_info {
    int32_t ppq;
    double bpm;
    size_t tempo_point_count;
    size_t time_signature_count;
    double tick_count;
    double second_count;
    int32_t bar_count;
    uint64_t revision;
} svs_score_info;

typedef struct svs_pitch_point {
    double tick;
    double pitch;
} svs_pitch_point;

typedef struct svs_pitch_segment {
    const svs_pitch_point* points;
    size_t count;
} svs_pitch_segment;

typedef struct svs_phoneme {
    svs_string_view symbol;
    double duration;
    double stretch_weight;
} svs_phoneme;

typedef struct svs_phoneme_list {
    const svs_phoneme* items;
    size_t count;
    size_t leading_count;
} svs_phoneme_list;

typedef struct svs_phoneme_layout_note {
    double fill_start;
    double fill_end;
    const svs_phoneme* leading;
    size_t leading_count;
    const svs_phoneme* body;
    size_t body_count;
    double body_offset;
} svs_phoneme_layout_note;

typedef struct svs_phoneme_timing {
    double start;
    double end;
} svs_phoneme_timing;

typedef struct svs_lyric_result {
    svs_string_view lyric;
    svs_string_view pronunciation;
    const svs_string_view* candidates;
    size_t candidate_count;
} svs_lyric_result;

typedef enum svs_automation_shape {
    SVS_AUTOMATION_CONTINUOUS = 0,
    SVS_AUTOMATION_PIECEWISE = 1
} svs_automation_shape;

typedef enum svs_property_kind {
    SVS_PROPERTY_NUMBER = 0,
    SVS_PROPERTY_TEXT = 1
} svs_property_kind;

typedef struct svs_automation_config {
    svs_string_view id;
    svs_string_view display_name;
    double min_value;
    double max_value;
    double default_value;
    svs_string_view color;
    svs_automation_shape shape;
} svs_automation_config;

typedef struct svs_property_config {
    svs_string_view id;
    svs_string_view display_name;
    svs_property_kind kind;
    double default_number;
    svs_string_view default_text;
} svs_property_config;

typedef struct svs_image {
    svs_string_view mime_type;
    svs_string_view path;
    const unsigned char* data;
    size_t size;
} svs_image;

typedef struct svs_voice_source_info {
    svs_string_view id;
    svs_string_view name;
    svs_string_view description;
    svs_image avatar;
    svs_image portrait;
} svs_voice_source_info;

typedef struct svs_format_info {
    svs_string_view plugin_id;
    svs_string_view name;
    svs_string_view extension;
} svs_format_info;

typedef struct svs_core_api {
    uint32_t size;
    uint32_t abi_version;
    svs_status (*context_create)(svs_context** out_context);
    void (*context_destroy)(svs_context* context);
    const char* (*last_error_message)(const svs_context* context);
    svs_status (*score_create)(svs_context* context, svs_score** out_score);
    void (*score_destroy)(svs_score* score);
    svs_status (*score_create_track)(svs_score* score, svs_track** out_track);
    svs_status (*track_create_part)(svs_track* track, svs_part** out_part);
    svs_status (*part_create_note)(svs_part* part, double pos, double dur, int32_t pitch,
                                   const char* lyric, svs_note** out_note);
    svs_status (*tempo_set_point)(svs_score* score, double tick, double bpm);
    svs_status (*time_signature_set)(svs_score* score, int32_t bar, int32_t numerator,
                                     int32_t denominator);
} svs_core_api;

SVS_API const svs_core_api* svs_core_get_api(void);
SVS_API svs_status svs_context_create(svs_context** out_context);
SVS_API void svs_context_destroy(svs_context* context);
SVS_API const char* svs_last_error_message(const svs_context* context);
SVS_API uint64_t svs_context_revision(const svs_context* context);
SVS_API svs_status svs_context_set_engines_dir(svs_context* context, const char* path);
SVS_API svs_status svs_context_load_engines(svs_context* context);
SVS_API svs_status svs_context_get_voice_sources(svs_context* context,
                                                  const svs_voice_source_info** out_sources,
                                                  size_t* out_count);
SVS_API svs_status svs_context_get_formats(svs_context* context,
                                           const svs_format_info** out_formats,
                                           size_t* out_count);

SVS_API svs_status svs_score_create(svs_context* context, svs_score** out_score);
SVS_API void svs_score_destroy(svs_score* score);
SVS_API uint64_t svs_score_revision(const svs_score* score);
SVS_API svs_status svs_score_create_track(svs_score* score, svs_track** out_track);
SVS_API svs_status svs_tempo_set_point(svs_score* score, double tick, double bpm);
SVS_API svs_status svs_tempo_get_points(const svs_score* score,
                                        const svs_tempo_point** out_points, size_t* out_count);
SVS_API double svs_tempo_bpm_at(const svs_score* score, double tick);
SVS_API svs_status svs_time_signature_set(svs_score* score, int32_t bar, int32_t numerator,
                                          int32_t denominator);
SVS_API svs_status svs_time_signature_get(const svs_score* score,
                                          const svs_time_signature** out_signatures,
                                          size_t* out_count);
SVS_API double svs_score_tick_to_seconds(const svs_score* score, double tick);
SVS_API double svs_score_seconds_to_tick(const svs_score* score, double seconds);
SVS_API double svs_score_tick_to_beat(const svs_score* score, double tick);
SVS_API double svs_score_beat_to_tick(const svs_score* score, double beat);
SVS_API svs_status svs_score_tick_to_bar_beat(const svs_score* score, double tick,
                                               svs_bar_beat* out_position);
SVS_API double svs_score_bar_to_tick(const svs_score* score, int32_t bar);
SVS_API svs_status svs_score_get_info(const svs_score* score, svs_score_info* out_info);
SVS_API svs_status svs_track_create_part(svs_track* track, svs_part** out_part);
SVS_API svs_status svs_part_create_note(svs_part* part, double pos, double dur, int32_t pitch,
                                         const char* lyric, svs_note** out_note);
SVS_API svs_status svs_note_remove(svs_part* part, svs_note* note);
SVS_API svs_status svs_part_get_notes(svs_part* part, const svs_note* const** out_notes,
                                      size_t* out_count);
SVS_API svs_status svs_part_move_notes(svs_part* part, svs_note* const* notes, size_t count,
                                       double delta_tick, int32_t delta_pitch);
SVS_API svs_status svs_part_begin_batch(svs_part* part);
SVS_API svs_status svs_part_end_batch(svs_part* part);
SVS_API svs_status svs_part_pitch_set_segments(svs_part* part,
                                                const svs_pitch_segment* segments, size_t count);
SVS_API svs_status svs_part_pitch_get_segments(svs_part* part,
                                                const svs_pitch_segment** out_segments,
                                                size_t* out_count);
SVS_API svs_status svs_part_pitch_add_point(svs_part* part, size_t segment_index,
                                            double tick, double pitch);
SVS_API svs_status svs_part_set_automation_configs(svs_part* part,
                                                    const svs_automation_config* configs,
                                                    size_t count);
SVS_API svs_status svs_part_get_automation_configs(svs_part* part,
                                                    const svs_automation_config** out_configs,
                                                    size_t* out_count);
SVS_API svs_status svs_part_get_automation(svs_part* part, const char* id,
                                            svs_automation** out_automation);
SVS_API svs_status svs_automation_set_default_value(svs_automation* automation, double value);
SVS_API double svs_automation_default_value(const svs_automation* automation);
SVS_API svs_status svs_automation_set_points(svs_automation* automation,
                                             const svs_pitch_point* points, size_t count);
SVS_API svs_status svs_automation_get_points(const svs_automation* automation,
                                             const svs_pitch_point** out_points, size_t* out_count);
SVS_API svs_status svs_automation_set_segments(svs_automation* automation,
                                               const svs_pitch_segment* segments, size_t count);
SVS_API svs_status svs_automation_get_segments(svs_automation* automation,
                                               const svs_pitch_segment** out_segments,
                                               size_t* out_count);
SVS_API double svs_automation_evaluate(const svs_automation* automation, double tick);
SVS_API svs_status svs_part_set_note_property_configs(svs_part* part,
                                                       const svs_property_config* configs,
                                                       size_t count);
SVS_API svs_status svs_part_get_note_property_configs(svs_part* part,
                                                       const svs_property_config** out_configs,
                                                       size_t* out_count);
SVS_API svs_status svs_note_get_info(const svs_note* note, svs_note_info* out_info);
SVS_API svs_status svs_note_set_pos(svs_note* note, double pos);
SVS_API svs_status svs_note_set_dur(svs_note* note, double dur);
SVS_API svs_status svs_note_set_pitch(svs_note* note, int32_t pitch);
SVS_API svs_status svs_note_set_lyric(svs_note* note, const char* lyric);
SVS_API svs_status svs_note_set_pronunciation(svs_note* note, const char* pronunciation);
SVS_API svs_status svs_note_property_set_double(svs_note* note, const char* key, double value);
SVS_API svs_status svs_note_property_get_double(const svs_note* note, const char* key,
                                                 double* out_value);
SVS_API svs_status svs_note_property_set_string(svs_note* note, const char* key,
                                                 const char* value);
SVS_API svs_status svs_note_property_get_string(const svs_note* note, const char* key,
                                                 svs_string_view* out_value);
SVS_API svs_status svs_note_get_phonemes(svs_note* note, svs_phoneme_list* out_phonemes);
SVS_API svs_status svs_note_phoneme_set(svs_note* note, int32_t slot, const svs_phoneme* phoneme);
SVS_API svs_status svs_note_phoneme_insert(svs_note* note, int32_t slot, const svs_phoneme* phoneme);
SVS_API svs_status svs_note_phoneme_remove(svs_note* note, int32_t slot);
SVS_API svs_status svs_note_set_body_offset(svs_note* note, double seconds);
SVS_API double svs_note_body_offset(const svs_note* note);
SVS_API svs_status svs_g2p_split_and_convert(svs_context* context, const char* text,
                                              const svs_lyric_result** out_results,
                                              size_t* out_count);
SVS_API svs_status svs_g2p_predict_syllable(svs_context* context, const char* pronunciation,
                                            svs_phoneme_list* out_phonemes);
SVS_API svs_status svs_part_apply_lyrics_batch(svs_part* part, size_t from_note_index,
                                               const char* text);
SVS_API svs_status svs_phoneme_layout_resolve(svs_context* context,
                                               const svs_phoneme_layout_note* notes,
                                               size_t note_count,
                                               const svs_phoneme_timing** out_timings,
                                               size_t* out_count);
SVS_API uint64_t svs_note_revision(const svs_note* note);

#ifdef __cplusplus
}
#endif

#endif