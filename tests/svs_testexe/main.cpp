#include "svs_core.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

bool expect(svs_status actual, svs_status expected, const char* step) {
    if (actual == expected) return true;
    std::cerr << step << " failed with status " << actual << '\n';
    return false;
}

bool expect(bool condition, const char* step) {
       if (condition) return true;
       std::cerr << step << " assertion failed\n";
       return false;
}

bool near(double actual, double expected) {
       return std::abs(actual - expected) < 1e-9;
}

} // namespace

int main() {
    svs_context* context = nullptr;
    if (!expect(svs_context_create(&context), SVS_OK, "context creation")) return 1;

       const svs_voice_source_info* voice_sources = nullptr;
       const svs_format_info* formats = nullptr;
       size_t voice_source_count = 0;
       size_t format_count = 0;
          const svs_status engine_load_status = svs_context_load_engines(context);
          if (engine_load_status != SVS_OK) {
                 std::cerr << "engine load error: " << svs_last_error_message(context) << '\n';
          }
          bool passed = expect(engine_load_status, SVS_OK, "engine load") &&
                              expect(svs_context_get_voice_sources(context, &voice_sources, &voice_source_count), SVS_OK,
                                           "voice source query") &&
                              expect(svs_context_get_formats(context, &formats, &format_count), SVS_OK, "format query") &&
                                expect(voice_source_count == 2 && format_count == 2 &&
                                           voice_sources[0].id.size == 12 && voice_sources[0].avatar.size == 4 &&
                                           voice_sources[0].portrait.size == 6 && voice_sources[0].avatar.mime_type.size == 9 &&
                                       formats[0].extension.size == 4 && voice_sources[1].id.size == 12 &&
                                       voice_sources[1].avatar.size == 4 && voice_sources[1].portrait.size == 6 &&
                                       formats[1].extension.size == 4, "plugin registry and images");

    svs_score* score = nullptr;
    svs_track* track = nullptr;
    svs_part* part = nullptr;
    svs_note* note = nullptr;
       svs_note* second_note = nullptr;
       passed = passed && expect(svs_score_create(context, &score), SVS_OK, "score creation") &&
                  expect(svs_score_create_track(score, &track), SVS_OK, "track creation") &&
                  expect(svs_track_create_part(track, &part), SVS_OK, "part creation") &&
                  expect(svs_part_create_note(part, 480, 240, 60, "la", &note), SVS_OK,
                         "note creation") &&
                  expect(svs_part_create_note(part, 960, 480, 62, "la", &second_note), SVS_OK,
                         "second note creation") &&
                  expect(svs_note_set_pronunciation(note, "la"), SVS_OK, "pronunciation write") &&
                  expect(svs_note_property_set_double(note, "velocity", 0.75), SVS_OK,
                         "number property write") &&
                  expect(svs_note_property_set_string(note, "style", "soft"), SVS_OK,
                         "string property write");

    passed = passed && expect(svs_tempo_set_point(score, 960, 60), SVS_OK, "tempo write") &&
             expect(svs_time_signature_set(score, 2, 3, 4), SVS_OK, "time signature write");

    double velocity = 0;
    svs_string_view style{};
    svs_note_info info{};
    passed = passed && expect(svs_note_property_get_double(note, "velocity", &velocity), SVS_OK,
                              "number property read") &&
             expect(svs_note_property_get_string(note, "style", &style), SVS_OK,
                    "string property read") &&
             expect(svs_note_get_info(note, &info), SVS_OK, "note info read") &&
             velocity == 0.75 && style.size == 4 && std::strncmp(style.data, "soft", style.size) == 0 &&
             info.pos == 480 && info.dur == 240 && info.pitch == 60 &&
             svs_context_revision(context) > 0;

       const svs_tempo_point* tempos = nullptr;
       const svs_time_signature* signatures = nullptr;
       size_t tempo_count = 0;
       size_t signature_count = 0;
       svs_bar_beat bar_beat{};
       svs_score_info score_info{};
       passed = passed && expect(svs_tempo_get_points(score, &tempos, &tempo_count), SVS_OK,
                                                   "tempo query") &&
                      expect(svs_time_signature_get(score, &signatures, &signature_count), SVS_OK,
                                   "time signature query") &&
                           expect(svs_score_tick_to_bar_beat(score, 3840, &bar_beat), SVS_OK,
                                   "tick to bar and beat") &&
                      expect(svs_score_get_info(score, &score_info), SVS_OK, "score info query") &&
                           expect(tempo_count == 2 && near(tempos[0].bpm, 120) && near(tempos[1].tick, 960),
                                  "tempo table") &&
                           expect(signature_count == 2 && signatures[1].bar == 2 && signatures[1].numerator == 3,
                                  "time signature table") &&
                           expect(near(svs_score_tick_to_seconds(score, 960), 1), "tempo boundary time") &&
                           expect(near(svs_score_tick_to_seconds(score, 1440), 2), "tempo segment time") &&
                           expect(near(svs_score_seconds_to_tick(score, 2), 1440), "seconds to tick") &&
                           expect(near(svs_score_tick_to_seconds(score, -480), -0.5), "negative tick time") &&
                           expect(near(svs_score_tick_to_beat(score, 3840), 8), "tick to beat") &&
                           expect(near(svs_score_beat_to_tick(score, 8), 3840), "beat to tick") &&
                           expect(bar_beat.bar == 2 && near(bar_beat.beat, 0), "tick to bar and beat result") &&
                           expect(near(svs_score_bar_to_tick(score, 2), 3840), "bar to tick") &&
                           expect(score_info.ppq == SVS_PPQ && near(score_info.tick_count, 1440) &&
                                      near(score_info.second_count, 2) && score_info.bar_count == 1,
                                  "score info values");

    const uint64_t revision_before_batch = svs_score_revision(score);
    svs_note* moved_notes[] = {note, second_note};
    const svs_pitch_point pitch_points[] = {{0, 60}, {240, 61}};
    const svs_pitch_segment pitch_segments[] = {{pitch_points, 2}};
    const svs_note* const* notes = nullptr;
    const svs_pitch_segment* returned_pitch_segments = nullptr;
    size_t note_count = 0;
    size_t returned_segment_count = 0;
    passed = passed && expect(svs_part_begin_batch(part), SVS_OK, "batch begin") &&
             expect(svs_part_move_notes(part, moved_notes, 2, 120, 1), SVS_OK, "batch move") &&
             expect(svs_part_apply_lyrics_batch(part, 0, "\xE4\xBD\xA0\xE5\xA5\xBD"), SVS_OK,
                    "batch lyrics") &&
             expect(svs_part_end_batch(part), SVS_OK, "batch end") &&
             expect(svs_part_get_notes(part, &notes, &note_count), SVS_OK, "sorted note query") &&
             expect(svs_part_pitch_set_segments(part, pitch_segments, 1), SVS_OK, "pitch segments write") &&
             expect(svs_part_pitch_add_point(part, 0, 480, 62), SVS_OK, "pitch point write") &&
             expect(svs_part_pitch_get_segments(part, &returned_pitch_segments, &returned_segment_count), SVS_OK,
                    "pitch segments query") &&
             expect(note_count == 2 && notes[0] == note && revision_before_batch < svs_score_revision(score),
                    "batch revision and ordering") &&
             expect(returned_segment_count == 1 && returned_pitch_segments[0].count == 3,
                    "pitch segments result");

    const svs_phoneme leading = {{"l", 1}, 0.08, 0};
    const svs_phoneme body = {{"a", 1}, 0.16, 1};
    svs_phoneme_list phonemes{};
    passed = passed && expect(svs_note_phoneme_insert(note, -1, &leading), SVS_OK, "leading phoneme insert") &&
             expect(svs_note_phoneme_insert(note, 0, &body), SVS_OK, "body phoneme insert") &&
             expect(svs_note_set_body_offset(note, -0.02), SVS_OK, "body offset write") &&
             expect(svs_note_get_phonemes(note, &phonemes), SVS_OK, "phoneme query") &&
             expect(phonemes.count == 2 && phonemes.leading_count == 1 && phonemes.items[1].symbol.size == 1,
                    "phoneme slot result");

    const svs_lyric_result* lyrics = nullptr;
    const svs_phoneme_timing* timings = nullptr;
    size_t lyric_count = 0;
    size_t timing_count = 0;
    const svs_phoneme_layout_note layout_note = {0, 0.48, &leading, 1, &body, 1, 0};
    passed = passed && expect(svs_g2p_split_and_convert(context, "\xE4\xBD\xA0\xE9\x87\x8D\xE3\x81\x8D\xE3\x82\x83",
                                                         &lyrics, &lyric_count), SVS_OK, "G2P conversion") &&
             expect(lyric_count == 3 && lyrics[0].pronunciation.size == 3 && lyrics[1].candidate_count == 2,
                    "G2P result") &&
             expect(svs_g2p_predict_syllable(context, "kya", &phonemes), SVS_OK, "G2P phoneme prediction") &&
             expect(svs_phoneme_layout_resolve(context, &layout_note, 1, &timings, &timing_count), SVS_OK,
                    "phoneme layout") &&
             expect(phonemes.count == 2 && phonemes.leading_count == 1 && timing_count == 2 &&
                    near(timings[0].end, 0) && near(timings[1].start, 0) && near(timings[1].end, 0.48),
                    "phoneme layout result");

    const svs_automation_config automation_configs[] = {
        {{"gain", 4}, {"Gain", 4}, 0, 1, 0.5, {"#00AA00", 7}, SVS_AUTOMATION_CONTINUOUS},
        {{"pitchDelta", 10}, {"Pitch delta", 11}, -24, 24, NAN, {"#FF8800", 7}, SVS_AUTOMATION_PIECEWISE},
    };
    const svs_property_config property_configs[] = {
        {{"tension", 7}, {"Tension", 7}, SVS_PROPERTY_NUMBER, 0.5, {"", 0}},
        {{"style", 5}, {"Style", 5}, SVS_PROPERTY_TEXT, 0, {"normal", 6}},
    };
    const svs_pitch_point gain_points[] = {{0, -0.1}, {480, 0.1}};
    const svs_pitch_point pitch_delta_points[] = {{960, 0}, {1440, 1}};
    const svs_pitch_segment pitch_delta_segments[] = {{pitch_delta_points, 2}};
    svs_automation* gain = nullptr;
    svs_automation* pitch_delta = nullptr;
    const svs_automation_config* returned_automation_configs = nullptr;
    const svs_property_config* returned_property_configs = nullptr;
    const svs_pitch_point* returned_gain_points = nullptr;
    const svs_pitch_segment* returned_automation_segments = nullptr;
    size_t automation_config_count = 0;
    size_t property_config_count = 0;
    size_t gain_point_count = 0;
    size_t automation_segment_count = 0;
    passed = passed && expect(svs_part_set_automation_configs(part, automation_configs, 2), SVS_OK,
                             "automation configs write") &&
             expect(svs_part_get_automation_configs(part, &returned_automation_configs, &automation_config_count), SVS_OK,
                    "automation configs query") &&
             expect(svs_part_get_automation(part, "gain", &gain), SVS_OK, "continuous automation query") &&
             expect(svs_part_get_automation(part, "pitchDelta", &pitch_delta), SVS_OK, "piecewise automation query") &&
             expect(svs_automation_set_points(gain, gain_points, 2), SVS_OK, "continuous points write") &&
             expect(svs_automation_set_default_value(gain, 0.6), SVS_OK, "continuous default write") &&
             expect(svs_automation_get_points(gain, &returned_gain_points, &gain_point_count), SVS_OK,
                    "continuous points query") &&
             expect(svs_automation_set_segments(pitch_delta, pitch_delta_segments, 1), SVS_OK,
                    "piecewise segments write") &&
             expect(svs_automation_get_segments(pitch_delta, &returned_automation_segments, &automation_segment_count), SVS_OK,
                    "piecewise segments query") &&
             expect(automation_config_count == 2 && returned_automation_configs[0].shape == SVS_AUTOMATION_CONTINUOUS &&
                    returned_automation_configs[1].shape == SVS_AUTOMATION_PIECEWISE, "automation config result") &&
             expect(gain_point_count == 2 && near(svs_automation_evaluate(gain, 240), 0.6) &&
                    near(svs_automation_evaluate(gain, 480), 0.7), "continuous automation evaluation") &&
             expect(automation_segment_count == 1 && returned_automation_segments[0].count == 2 &&
                    near(svs_automation_evaluate(pitch_delta, 1200), 0.5) && std::isnan(svs_automation_evaluate(pitch_delta, 720)),
                    "piecewise automation evaluation") &&
             expect(svs_part_set_note_property_configs(part, property_configs, 2), SVS_OK, "property configs write") &&
             expect(svs_part_get_note_property_configs(part, &returned_property_configs, &property_config_count), SVS_OK,
                    "property configs query") &&
             expect(svs_note_property_set_double(note, "tension", 0.25), SVS_OK, "schema number lane write") &&
             expect(svs_note_property_get_double(note, "tension", &velocity), SVS_OK, "schema number lane query") &&
             expect(property_config_count == 2 && returned_property_configs[0].kind == SVS_PROPERTY_NUMBER &&
                    returned_property_configs[1].kind == SVS_PROPERTY_TEXT && near(velocity, 0.25),
                    "property schema result");

    svs_score_destroy(score);
    svs_context_destroy(context);
    if (!passed) {
              std::cerr << "SVS Core smoke test assertions failed.\n";
        return 1;
    }
              std::cout << "SVS Core M1, M2, M3, M4, M5 and M6 smoke test passed.\n";
    return 0;
}