#include "svs_core.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

QString text_view(svs_string_view value) {
    if (value.data == nullptr) return {};
    return QString::fromUtf8(value.data, static_cast<int>(value.size));
}

QString image_summary(const svs_image& image) {
    if (image.data != nullptr && image.size > 0) {
        return text_view(image.mime_type) + " / " + QString::number(static_cast<qulonglong>(image.size)) + " bytes";
    }
    if (image.path.data != nullptr && image.path.size > 0) return "file: " + text_view(image.path);
    return "none";
}

QString shape_text(svs_automation_shape shape) {
    return shape == SVS_AUTOMATION_PIECEWISE ? "piecewise" : "continuous";
}

class PianoRollWidget final : public QWidget {
public:
    using ChangedHandler = std::function<void()>;
    using SelectionHandler = std::function<void(int)>;

    explicit PianoRollWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(1180, total_height());
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void set_model(svs_context* context, svs_score* score, svs_part* part) {
        m_context = context;
        m_score = score;
        m_part = part;
        update();
    }

    void set_selection(int index) {
        m_selected_index = index;
        update();
    }

    int selection() const { return m_selected_index; }

    ChangedHandler on_changed;
    SelectionHandler on_selection_changed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#121522"));

        draw_header(painter);
        draw_keyboard_grid(painter);
        draw_notes(painter);
        draw_pitch_curve(painter);
        draw_phonemes(painter, phoneme_top());
        draw_parameter_lanes(painter);
        draw_portrait(painter);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (m_part == nullptr) return;
        const QPointF position = event->position();
        const int hit_index = note_at(position);
        if (event->button() == Qt::RightButton) {
            if (hit_index >= 0) {
                const svs_note* const* notes = nullptr;
                size_t note_count = 0;
                if (svs_part_get_notes(m_part, &notes, &note_count) == SVS_OK &&
                    static_cast<size_t>(hit_index) < note_count) {
                    svs_note_remove(m_part, const_cast<svs_note*>(notes[hit_index]));
                    m_selected_index = -1;
                    notify_selection();
                    notify_changed();
                }
            }
            return;
        }
        if (event->button() != Qt::LeftButton) return;

        if (hit_index < 0) {
            if (position.y() < header_height || position.y() >= header_height + note_area_height()) return;
            const double tick = quantize_tick(x_to_tick(position.x()));
            const int pitch = pitch_from_y(position.y());
            svs_note* created_note = nullptr;
            if (svs_part_create_note(m_part, tick, 480, pitch, "la", &created_note) == SVS_OK) {
                m_selected_index = find_note_index(created_note);
                notify_selection();
                notify_changed();
            }
            return;
        }

        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK ||
            static_cast<size_t>(hit_index) >= note_count) return;
        svs_note_info info{};
        const svs_note* note = notes[hit_index];
        if (svs_note_get_info(note, &info) != SVS_OK) return;
        m_selected_index = hit_index;
        m_dragging_note = const_cast<svs_note*>(note);
        m_dragging = true;
        m_resizing = std::abs(position.x() - tick_to_x(info.pos + info.dur)) < 10;
        m_drag_anchor_tick = x_to_tick(position.x());
        m_drag_initial = info;
        notify_selection();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_dragging || m_dragging_note == nullptr ||
            !(event->buttons() & Qt::LeftButton)) return;
        const double current_tick = x_to_tick(event->position().x());
        if (m_resizing) {
            const double duration = quantize_tick(current_tick - m_drag_initial.pos);
            svs_note_set_dur(m_dragging_note, (std::max)(120.0, duration));
        } else {
            const double position = quantize_tick(m_drag_initial.pos + current_tick - m_drag_anchor_tick);
            svs_note_set_pos(m_dragging_note, (std::max)(0.0, position));
            svs_note_set_pitch(m_dragging_note, pitch_from_y(event->position().y()));
        }
        notify_changed();
        update();
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
        m_resizing = false;
        m_dragging_note = nullptr;
    }

private:
    static constexpr int header_height = 32;
    static constexpr int key_height = 13;
    static constexpr int pitch_min = 48;
    static constexpr int pitch_max = 84;
    static constexpr int left_margin = 48;
    static constexpr int portrait_width = 300;
    static constexpr int phoneme_height = 64;
    static constexpr int parameter_height = 156;
    static constexpr double tick_scale = 0.16;

    int note_area_height() const { return (pitch_max - pitch_min + 1) * key_height; }
    int total_height() const { return header_height + note_area_height() + phoneme_height + parameter_height + 16; }
    int phoneme_top() const { return header_height + note_area_height() + 8; }
    int parameter_top() const { return phoneme_top() + phoneme_height + 8; }
    int content_right() const { return (std::max)(left_margin + 120, width() - portrait_width); }
    int pitch_to_y(int pitch) const { return header_height + (pitch_max - pitch) * key_height; }
    int pitch_from_y(double y) const {
        const int pitch = pitch_max - static_cast<int>((y - header_height) / key_height);
        return (std::clamp)(pitch, pitch_min, pitch_max);
    }
    double tick_to_x(double tick) const { return left_margin + tick * tick_scale; }
    double x_to_tick(double x) const { return (x - left_margin) / tick_scale; }
    double quantize_tick(double tick) const { return std::round(tick / 120.0) * 120.0; }

    void draw_header(QPainter& painter) const {
        painter.fillRect(0, 0, width(), header_height, QColor("#1b1d2b"));
        painter.fillRect(0, 0, left_margin, header_height, QColor("#2a2b3c"));
        painter.setPen(QColor("#d5d8e7"));
        painter.drawText(11, 13, "4/4");
        painter.setPen(QColor("#8d91a7"));
        painter.drawText(11, 26, "120.00");
        painter.setPen(QColor("#5c6279"));
        painter.drawLine(left_margin, header_height - 1, content_right(), header_height - 1);
        for (int bar = 0; bar <= 12; ++bar) {
            const int x = static_cast<int>(tick_to_x(bar * 1920));
            if (x > content_right()) break;
            painter.setPen(QColor("#b1b5c8"));
            painter.drawText(x + 5, 13, QString::number(bar + 1));
            painter.setPen(QColor("#5c6279"));
            painter.drawLine(x, header_height - 8, x, header_height);
        }
    }

    void draw_keyboard_grid(QPainter& painter) const {
        for (int pitch = pitch_max; pitch >= pitch_min; --pitch) {
            const int y = pitch_to_y(pitch);
            const bool black_key = is_black_key(pitch);
            painter.fillRect(left_margin, y, content_right() - left_margin, key_height,
                             black_key ? QColor("#1c1e30") : QColor("#23253a"));
            painter.setPen(QColor("#30334c"));
            painter.drawLine(left_margin, y, content_right(), y);

            painter.fillRect(0, y, left_margin, key_height,
                             black_key ? QColor("#12131b") : QColor("#dedfe5"));
            if (!black_key) {
                painter.setPen(QColor("#898b99"));
                painter.drawLine(0, y + key_height - 1, left_margin, y + key_height - 1);
            }
            painter.setPen(black_key ? QColor("#e0e2e9") : QColor("#333544"));
            painter.drawText(7, y + 10, pitch % 12 == 0 ? "C" + QString::number(pitch / 12 - 1) : "");
        }
        for (int tick = 0; tick <= 12 * 1920; tick += 120) {
            const int x = static_cast<int>(tick_to_x(tick));
            if (x > content_right()) break;
            const bool bar = tick % 1920 == 0;
            const bool beat = tick % 480 == 0;
            painter.setPen(bar ? QColor("#555975") : beat ? QColor("#383b56") : QColor("#292c45"));
            painter.drawLine(x, header_height, x, header_height + note_area_height());
        }
        for (int key = 0; key <= 12 * 1920; key += 480) {
            const int x = static_cast<int>(tick_to_x(key));
            if (x > content_right()) break;
            painter.setPen(QColor("#4d506b"));
            painter.drawLine(x, header_height, x, header_height + note_area_height());
        }
        painter.setPen(QColor("#555975"));
        painter.drawRect(left_margin, header_height, content_right() - left_margin - 1, note_area_height());

        for (int pitch = pitch_min; pitch <= pitch_max; ++pitch) {
            if (!is_black_key(pitch)) continue;
            const int y = pitch_to_y(pitch) + key_height - 5;
            painter.setBrush(QColor("#171821"));
            painter.setPen(Qt::NoPen);
            painter.drawRect(0, y, left_margin * 3 / 5, 9);
        }
    }

    void draw_notes(QPainter& painter) const {
        if (m_part == nullptr) return;
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK) return;
        for (size_t index = 0; index < note_count; ++index) {
            svs_note_info info{};
            if (svs_note_get_info(notes[index], &info) != SVS_OK) continue;
            const QRectF note_rect(tick_to_x(info.pos), pitch_to_y(info.pitch) + 1,
                                   (std::max)(8.0, info.dur * tick_scale), key_height - 2);
            if (note_rect.left() >= content_right() || note_rect.right() < left_margin) continue;
            const bool selected = static_cast<int>(index) == m_selected_index;
            painter.setPen(selected ? QColor("#9c9bff") : QColor("#7374e4"));
            painter.setBrush(selected ? QColor("#5c5bd0") : QColor("#42448e"));
            painter.drawRoundedRect(note_rect, 2, 2);
            painter.setPen(selected ? QColor("#ffffff") : QColor("#d9daf5"));
            const QString lyric = text_view(info.lyric);
            painter.drawText(note_rect.adjusted(5, 0, -3, 0), Qt::AlignVCenter,
                             lyric.isEmpty() ? "note" : lyric);
        }
    }

    void draw_pitch_curve(QPainter& painter) const {
        if (m_part == nullptr) return;
        const svs_pitch_segment* segments = nullptr;
        size_t segment_count = 0;
        if (svs_part_pitch_get_segments(m_part, &segments, &segment_count) != SVS_OK) return;
        painter.setPen(QPen(QColor("#a8a7ef"), 1.5));
        for (size_t segment_index = 0; segment_index < segment_count; ++segment_index) {
            const auto& segment = segments[segment_index];
            if (segment.points == nullptr || segment.count == 0) continue;
            QPainterPath path;
            path.moveTo(tick_to_x(segment.points[0].tick),
                        header_height + (pitch_max - segment.points[0].pitch) * key_height + key_height / 2.0);
            for (size_t point_index = 1; point_index < segment.count; ++point_index) {
                const auto& point = segment.points[point_index];
                const double y = header_height + (pitch_max - point.pitch) * key_height + key_height / 2.0;
                path.lineTo(tick_to_x(point.tick), y);
            }
            painter.drawPath(path);
        }
    }

    void draw_phonemes(QPainter& painter, int top) const {
        painter.fillRect(0, top - 8, width(), phoneme_height, QColor("#0d1019"));
        painter.fillRect(0, top - 8, left_margin, phoneme_height, QColor("#202333"));
        painter.setPen(QColor("#aeb2c7"));
        painter.drawText(7, top + 15, "phonemes");
        painter.setPen(QColor("#383b52"));
        painter.drawLine(left_margin, top - 8, content_right(), top - 8);
        painter.drawLine(left_margin, top + phoneme_height - 1, content_right(), top + phoneme_height - 1);
        painter.setPen(QColor("#72778f"));
        painter.drawLine(left_margin, top + 28, content_right(), top + 28);
        for (int x = left_margin; x < content_right(); x += 3) {
            const double wave = std::sin(static_cast<double>(x) * 0.17) * 4.0 +
                                std::sin(static_cast<double>(x) * 0.047) * 2.5;
            painter.drawLine(x, top + 28, x, top + 28 + static_cast<int>(wave));
        }
        if (m_part == nullptr || m_selected_index < 0) return;
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK ||
            static_cast<size_t>(m_selected_index) >= note_count) return;
        svs_note_info note_info{};
        svs_phoneme_list phonemes{};
        if (svs_note_get_info(notes[m_selected_index], &note_info) != SVS_OK ||
            svs_note_get_phonemes(const_cast<svs_note*>(notes[m_selected_index]), &phonemes) != SVS_OK) return;
        if (phonemes.count == 0) return;
        const double note_width = (std::max)(120.0, note_info.dur * tick_scale);
        const double block_width = note_width / static_cast<double>(phonemes.count);
        for (size_t index = 0; index < phonemes.count; ++index) {
            const double x = tick_to_x(note_info.pos) + block_width * static_cast<double>(index);
            const QRectF block(x, top + 34, block_width - 3, 20);
            painter.setBrush(index < phonemes.leading_count ? QColor("#66578e") : QColor("#3d7e73"));
            painter.setPen(QColor("#d9dcef"));
            painter.drawRoundedRect(block, 3, 3);
            painter.drawText(block, Qt::AlignCenter, text_view(phonemes.items[index].symbol));
        }
    }

    void draw_parameter_lanes(QPainter& painter) const {
        const int lane_height = parameter_height / 3;
        painter.fillRect(0, parameter_top() - 8, width(), parameter_height + 8, QColor("#151827"));
        const QString labels[] = {"pitch", "gain", "tension"};
        const QColor colors[] = {QColor("#8e8bea"), QColor("#5ec7b3"), QColor("#e59a74")};
        for (int lane = 0; lane < 3; ++lane) {
            const int top = parameter_top() + lane * lane_height;
            painter.fillRect(0, top, left_margin, lane_height, QColor("#202333"));
            painter.setPen(QColor("#b1b4c9"));
            painter.drawText(7, top + 18, labels[lane]);
            painter.setPen(QColor("#2e3249"));
            painter.drawLine(left_margin, top, content_right(), top);
            painter.drawLine(left_margin, top + lane_height - 1, content_right(), top + lane_height - 1);
            painter.setPen(QPen(colors[lane], lane == 0 ? 1.5 : 1.2));
            QPainterPath path;
            bool has_point = false;
            if (lane == 0) {
                const svs_pitch_segment* segments = nullptr;
                size_t segment_count = 0;
                if (m_part != nullptr && svs_part_pitch_get_segments(m_part, &segments, &segment_count) == SVS_OK) {
                    for (size_t segment_index = 0; segment_index < segment_count; ++segment_index) {
                        const auto& segment = segments[segment_index];
                        if (segment.points == nullptr || segment.count == 0) continue;
                        for (size_t index = 0; index < segment.count; ++index) {
                            const auto& point = segment.points[index];
                            const double y = top + lane_height / 2.0 -
                                             (point.pitch - 64.0) * 2.0;
                            if (!has_point) {
                                path.moveTo(tick_to_x(point.tick), y);
                                has_point = true;
                            } else {
                                path.lineTo(tick_to_x(point.tick), y);
                            }
                        }
                    }
                }
            } else {
                svs_automation* automation = nullptr;
                const char* id = lane == 1 ? "gain" : "pitchDelta";
                if (m_part != nullptr && svs_part_get_automation(m_part, id, &automation) == SVS_OK) {
                    const svs_pitch_point* points = nullptr;
                    size_t point_count = 0;
                    if (svs_automation_get_points(automation, &points, &point_count) == SVS_OK) {
                        for (size_t index = 0; index < point_count; ++index) {
                            const auto& point = points[index];
                            const double y = top + lane_height / 2.0 - point.pitch * lane_height * 1.8;
                            if (!has_point) {
                                path.moveTo(tick_to_x(point.tick), y);
                                has_point = true;
                            } else {
                                path.lineTo(tick_to_x(point.tick), y);
                            }
                        }
                    }
                }
            }
            if (has_point) painter.drawPath(path);
        }
    }

    void draw_portrait(QPainter& painter) const {
        const int left = content_right();
        QLinearGradient background(left, 0, width(), 0);
        background.setColorAt(0, QColor(25, 26, 45, 0));
        background.setColorAt(0.32, QColor(32, 32, 57, 120));
        background.setColorAt(1, QColor(24, 25, 43, 235));
        painter.fillRect(left, header_height, width() - left, parameter_top() - header_height, background);
        painter.setPen(QColor("#aeb2c7"));
        painter.drawText(left + 18, header_height + 25, "VOICE PORTRAIT");

        const svs_voice_source_info* voices = nullptr;
        size_t voice_count = 0;
        QImage source_image;
        if (m_context != nullptr && svs_context_get_voice_sources(m_context, &voices, &voice_count) == SVS_OK &&
            voice_count > 0) {
            const svs_image& image = voices[0].portrait.size > 0 ? voices[0].portrait : voices[0].avatar;
            if (image.data != nullptr && image.size > 0) {
                source_image = QImage::fromData(image.data, static_cast<int>(image.size));
            }
        }
        if (!source_image.isNull()) {
            const QRect target(left + 10, header_height + 42, width() - left - 20, parameter_top() - header_height - 52);
            painter.drawImage(target, source_image.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }

        const double center = left + (width() - left) * 0.58;
        const double base = parameter_top() - 15;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(58, 61, 106, 200));
        QPainterPath cape;
        cape.moveTo(center - 112, base);
        cape.cubicTo(center - 90, base - 120, center - 62, base - 230, center - 42, base - 280);
        cape.lineTo(center + 55, base - 255);
        cape.cubicTo(center + 80, base - 180, center + 112, base - 90, center + 120, base);
        cape.closeSubpath();
        painter.drawPath(cape);
        painter.setBrush(QColor("#2a315f"));
        painter.drawRoundedRect(QRectF(center - 58, base - 220, 116, 215), 35, 35);
        painter.setBrush(QColor("#d8b6ac"));
        painter.drawEllipse(QRectF(center - 43, base - 318, 86, 92));
        painter.setBrush(QColor("#8793c5"));
        QPainterPath hair;
        hair.moveTo(center - 55, base - 275);
        hair.cubicTo(center - 72, base - 350, center - 32, base - 376, center + 5, base - 351);
        hair.cubicTo(center + 50, base - 384, center + 72, base - 331, center + 52, base - 274);
        hair.cubicTo(center + 32, base - 302, center + 10, base - 312, center - 15, base - 286);
        hair.closeSubpath();
        painter.drawPath(hair);
        painter.setBrush(QColor("#75b8c8"));
        painter.drawEllipse(QRectF(center - 27, base - 296, 8, 5));
        painter.drawEllipse(QRectF(center + 19, base - 296, 8, 5));
        painter.setBrush(QColor("#45549b"));
        painter.drawRect(QRectF(center - 45, base - 195, 90, 95));
        painter.setBrush(QColor("#d8b6ac"));
        painter.drawRect(QRectF(center - 56, base - 195, 11, 90));
        painter.drawRect(QRectF(center + 45, base - 195, 11, 90));
        painter.setBrush(QColor(147, 147, 220, 160));
        painter.drawEllipse(QRectF(center + 60, base - 230, 58, 58));
        painter.setPen(QPen(QColor(121, 133, 203, 130), 5));
        painter.drawArc(QRectF(center + 55, base - 250, 82, 135), 70, 220);
    }

    static bool is_black_key(int pitch) {
        const int octave_pitch = pitch % 12;
        return octave_pitch == 1 || octave_pitch == 3 || octave_pitch == 6 ||
               octave_pitch == 8 || octave_pitch == 10;
    }

    int note_at(QPointF point) const {
        if (m_part == nullptr || point.y() < header_height ||
            point.y() >= header_height + note_area_height() || point.x() >= content_right()) return -1;
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK) return -1;
        for (size_t index = note_count; index > 0; --index) {
            svs_note_info info{};
            if (svs_note_get_info(notes[index - 1], &info) != SVS_OK) continue;
            const QRectF note_rect(tick_to_x(info.pos), pitch_to_y(info.pitch),
                                   (std::max)(8.0, info.dur * tick_scale), key_height);
            if (note_rect.contains(point)) return static_cast<int>(index - 1);
        }
        return -1;
    }

    int find_note_index(const svs_note* target) const {
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (m_part == nullptr || svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK) return -1;
        for (size_t index = 0; index < note_count; ++index) {
            if (notes[index] == target) return static_cast<int>(index);
        }
        return -1;
    }

    void notify_changed() {
        if (on_changed) on_changed();
    }

    void notify_selection() {
        if (on_selection_changed) on_selection_changed(m_selected_index);
    }

    svs_context* m_context = nullptr;
    svs_score* m_score = nullptr;
    svs_part* m_part = nullptr;
    int m_selected_index = -1;
    bool m_dragging = false;
    bool m_resizing = false;
    svs_note* m_dragging_note = nullptr;
    double m_drag_anchor_tick = 0;
    svs_note_info m_drag_initial{};
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("SVS Core Qt Piano Roll");
        resize(1360, 900);
        initialize_core();
        build_ui();
        refresh_all();
    }

    ~MainWindow() override {
        if (m_score != nullptr) svs_score_destroy(m_score);
        if (m_context != nullptr) svs_context_destroy(m_context);
    }

private:
    void initialize_core() {
        if (svs_context_create(&m_context) != SVS_OK) return;
        m_engine_status = svs_context_load_engines(m_context);
        if (svs_score_create(m_context, &m_score) != SVS_OK ||
            svs_score_create_track(m_score, &m_track) != SVS_OK ||
            svs_track_create_part(m_track, &m_part) != SVS_OK) return;
        seed_document();
    }

    void seed_document() {
        const struct NoteSeed { double pos; double dur; int pitch; const char* lyric; } seeds[] = {
            {0, 480, 60, "la"}, {480, 480, 62, "la"}, {960, 720, 64, "la"},
            {1680, 960, 67, "la"}, {2640, 480, 69, "la"},
        };
        for (const auto& seed : seeds) {
            svs_note* note = nullptr;
            if (svs_part_create_note(m_part, seed.pos, seed.dur, seed.pitch, seed.lyric, &note) != SVS_OK) continue;
            svs_note_property_set_double(note, "tension", 0.5);
            const svs_phoneme body = {{"a", 1}, 0.16, 1};
            svs_note_phoneme_insert(note, 0, &body);
        }
        const svs_pitch_point pitch_points[] = {{0, 60}, {480, 62}, {960, 64}, {1680, 67}, {2640, 69}};
        const svs_pitch_segment pitch_segments[] = {{pitch_points, 5}};
        svs_part_pitch_set_segments(m_part, pitch_segments, 1);

        const svs_automation_config automation_configs[] = {
            {{"gain", 4}, {"Gain", 4}, 0, 1, 0.5, {"#59c3c3", 7}, SVS_AUTOMATION_CONTINUOUS},
            {{"pitchDelta", 10}, {"Pitch delta", 11}, -24, 24, NAN, {"#ff876c", 7}, SVS_AUTOMATION_PIECEWISE},
        };
        svs_part_set_automation_configs(m_part, automation_configs, 2);
        svs_automation* gain = nullptr;
        if (svs_part_get_automation(m_part, "gain", &gain) == SVS_OK) {
            const svs_pitch_point gain_points[] = {{0, -0.1}, {480, 0.1}, {960, 0.05}};
            svs_automation_set_points(gain, gain_points, 3);
        }

        const svs_property_config properties[] = {
            {{"tension", 7}, {"Tension", 7}, SVS_PROPERTY_NUMBER, 0.5, {"", 0}},
            {{"style", 5}, {"Style", 5}, SVS_PROPERTY_TEXT, 0, {"normal", 6}},
        };
        svs_part_set_note_property_configs(m_part, properties, 2);
    }

    void build_ui() {
        setWindowTitle("TuneLab - Piano");
        auto* root = new QWidget(this);
        auto* root_layout = new QVBoxLayout(root);
        root_layout->setContentsMargins(0, 0, 0, 0);
        root_layout->setSpacing(0);

        auto* top_bar = new QFrame(root);
        top_bar->setObjectName("topBar");
        top_bar->setFixedHeight(58);
        auto* top_layout = new QHBoxLayout(top_bar);
        top_layout->setContentsMargins(14, 0, 14, 0);
        top_layout->setSpacing(8);

        auto* product = new QLabel("TuneLab  /  Piano", top_bar);
        product->setObjectName("productLabel");
        product->setMinimumWidth(156);
        top_layout->addWidget(product);
        auto add_tool = [this, top_bar](QStyle::StandardPixmap icon, const QString& tooltip,
                                         bool checkable = false) {
            auto* button = new QToolButton(top_bar);
            button->setIcon(style()->standardIcon(icon));
            button->setIconSize(QSize(18, 18));
            button->setToolTip(tooltip);
            button->setCheckable(checkable);
            button->setAutoRaise(true);
            button->setFixedSize(32, 32);
            button->setCursor(Qt::PointingHandCursor);
            return button;
        };
        auto* play_button = add_tool(QStyle::SP_MediaPlay, "Play");
        auto* stop_button = add_tool(QStyle::SP_MediaStop, "Stop");
        top_layout->addWidget(play_button);
        top_layout->addWidget(stop_button);
        auto* previous_button = add_tool(QStyle::SP_ArrowBack, "Previous bar");
        auto* next_button = add_tool(QStyle::SP_ArrowForward, "Next bar");
        top_layout->addWidget(previous_button);
        top_layout->addWidget(next_button);
        m_time_label = new QLabel("00:00:00.000", top_bar);
        m_time_label->setObjectName("timeLabel");
        m_time_label->setMinimumWidth(110);
        top_layout->addWidget(m_time_label);

        auto* separator = new QFrame(top_bar);
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedHeight(24);
        top_layout->addSpacing(8);
        top_layout->addWidget(separator);
        top_layout->addSpacing(8);
        auto* pointer_button = add_tool(QStyle::SP_ArrowUp, "Select");
        auto* draw_button = add_tool(QStyle::SP_FileDialogDetailedView, "Draw notes", true);
        draw_button->setChecked(true);
        auto* curve_button = add_tool(QStyle::SP_DialogApplyButton, "Edit pitch curve");
        auto* erase_button = add_tool(QStyle::SP_DialogCancelButton, "Erase");
        top_layout->addWidget(pointer_button);
        top_layout->addWidget(draw_button);
        top_layout->addWidget(curve_button);
        top_layout->addWidget(erase_button);
        top_layout->addStretch(1);

        auto* voice_caption = new QLabel("VOICE", top_bar);
        voice_caption->setObjectName("toolbarCaption");
        top_layout->addWidget(voice_caption);
        m_voice_combo = new QComboBox(top_bar);
        m_voice_combo->setMinimumWidth(156);
        m_voice_combo->setToolTip("Voice source metadata");
        top_layout->addWidget(m_voice_combo);
        m_voice_details = new QLabel("--", top_bar);
        m_voice_details->setObjectName("toolbarDetail");
        m_voice_details->setMinimumWidth(112);
        top_layout->addWidget(m_voice_details);
        auto* reload_button = add_tool(QStyle::SP_BrowserReload, "Reload engine metadata");
        top_layout->addWidget(reload_button);
        m_inspector_button = add_tool(QStyle::SP_FileDialogDetailedView, "Show inspector", true);
        top_layout->addWidget(m_inspector_button);
        connect(reload_button, &QToolButton::clicked, this, [this] { reload_metadata(); });
        connect(play_button, &QToolButton::clicked, this, [this] {
            statusBar()->showMessage("M7 playback/session remains a manual GUI validation item.");
        });
        connect(stop_button, &QToolButton::clicked, this, [this] {
            statusBar()->showMessage("Playback stopped.");
        });
        connect(pointer_button, &QToolButton::clicked, this, [this] {
            statusBar()->showMessage("Select tool active.");
        });
        connect(curve_button, &QToolButton::clicked, this, [this] {
            statusBar()->showMessage("Pitch curve tool active.");
        });
        connect(erase_button, &QToolButton::clicked, this, [this] {
            statusBar()->showMessage("Right click a note to remove it.");
        });
        connect(m_voice_combo, &QComboBox::currentIndexChanged, this,
                [this](int row) { show_voice(row); });
        root_layout->addWidget(top_bar);

        auto* info_bar = new QFrame(root);
        info_bar->setObjectName("contextBar");
        info_bar->setFixedHeight(30);
        auto* info_layout = new QHBoxLayout(info_bar);
        info_layout->setContentsMargins(12, 0, 12, 0);
        info_layout->setSpacing(18);
        m_bpm_label = add_info_cell(info_layout, "BPM");
        m_signature_label = add_info_cell(info_layout, "Signature");
        m_ppq_label = add_info_cell(info_layout, "PPQ");
        m_length_label = add_info_cell(info_layout, "Length");
        m_engine_status_label = add_info_cell(info_layout, "Engine");
        m_format_summary = add_info_cell(info_layout, "Formats");
        root_layout->addWidget(info_bar);

        auto* roll_frame = new QFrame(root);
        roll_frame->setObjectName("rollFrame");
        auto* roll_layout = new QVBoxLayout(roll_frame);
        roll_layout->setContentsMargins(0, 0, 0, 0);
        m_roll = new PianoRollWidget(roll_frame);
        m_roll->set_model(m_context, m_score, m_part);
        m_roll->on_changed = [this] { refresh_all(); };
        m_roll->on_selection_changed = [this](int index) { select_note(index); };
        auto* roll_scroll = new QScrollArea(roll_frame);
        roll_scroll->setFrameShape(QFrame::NoFrame);
        roll_scroll->setWidget(m_roll);
        roll_scroll->setWidgetResizable(true);
        roll_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        roll_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        roll_layout->addWidget(roll_scroll);
        root_layout->addWidget(roll_frame, 1);

        auto* footer = new QFrame(root);
        footer->setObjectName("footerBar");
        footer->setFixedHeight(54);
        auto* footer_layout = new QHBoxLayout(footer);
        footer_layout->setContentsMargins(12, 7, 12, 7);
        footer_layout->setSpacing(7);
        m_selected_note_label = new QLabel("No note selected", footer);
        m_selected_note_label->setObjectName("selectionLabel");
        m_selected_note_label->setMinimumWidth(170);
        footer_layout->addWidget(m_selected_note_label);
        m_selected_lyric = new QLineEdit(footer);
        m_selected_lyric->setPlaceholderText("lyric");
        m_selected_lyric->setMaximumWidth(120);
        footer_layout->addWidget(m_selected_lyric);
        m_selected_pitch = new QSpinBox(footer);
        m_selected_pitch->setRange(0, 127);
        m_selected_pitch->setMaximumWidth(70);
        m_selected_pitch->setToolTip("MIDI pitch");
        footer_layout->addWidget(m_selected_pitch);
        auto* apply_note_button = new QPushButton("Apply note", footer);
        apply_note_button->setToolTip("Write lyric and pitch through SVS Core");
        footer_layout->addWidget(apply_note_button);
        auto* footer_separator = new QFrame(footer);
        footer_separator->setFrameShape(QFrame::VLine);
        footer_separator->setFrameShadow(QFrame::Plain);
        footer_separator->setFixedHeight(24);
        footer_layout->addWidget(footer_separator);
        m_batch_lyrics = new QLineEdit("ni hao wo", footer);
        m_batch_lyrics->setPlaceholderText("batch lyrics");
        m_batch_lyrics->setMinimumWidth(150);
        footer_layout->addWidget(m_batch_lyrics, 1);
        auto* lyrics_button = new QPushButton("Apply lyrics", footer);
        footer_layout->addWidget(lyrics_button);
        auto* pitch_button = new QPushButton("Write curve", footer);
        footer_layout->addWidget(pitch_button);
        auto* reset_button = new QPushButton("Reset", footer);
        footer_layout->addWidget(reset_button);
        connect(apply_note_button, &QPushButton::clicked, this, [this] { apply_selected_note(); });
        connect(lyrics_button, &QPushButton::clicked, this, [this] { apply_batch_lyrics(); });
        connect(pitch_button, &QPushButton::clicked, this, [this] { write_pitch_curve(); });
        connect(reset_button, &QPushButton::clicked, this, [this] { reset_document(); });
        root_layout->addWidget(footer);

        setCentralWidget(root);
        statusBar()->showMessage("Ready. M7 synthesis/session checks remain manual GUI work.");

        m_inspector_dock = new QDockWidget("Inspector", this);
        m_inspector_dock->setObjectName("inspectorDock");
        m_inspector_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
        m_inspector_dock->setWidget(build_inspector(m_inspector_dock));
        addDockWidget(Qt::BottomDockWidgetArea, m_inspector_dock);
        m_inspector_dock->hide();
        connect(m_inspector_button, &QToolButton::toggled, m_inspector_dock, &QDockWidget::setVisible);
        connect(m_inspector_dock, &QDockWidget::visibilityChanged, m_inspector_button,
            &QToolButton::setChecked);

        setStyleSheet(
            "QMainWindow { background: #10121d; color: #d9dbea; }"
            "QWidget { color: #d9dbea; }"
            "QFrame#topBar { background: #1a1c29; border-bottom: 1px solid #2c3046; }"
            "QFrame#contextBar { background: #25283b; border-bottom: 1px solid #343852; }"
            "QFrame#rollFrame { background: #111321; }"
            "QFrame#footerBar { background: #292b3e; border-top: 1px solid #3c405b; }"
            "QDockWidget { background: #191b2a; color: #d9dbea; }"
            "QDockWidget::title { background: #25283b; padding: 6px; }"
            "QLabel#productLabel { color: #f3f4fa; font-size: 15px; font-weight: 600; }"
            "QLabel#toolbarCaption { color: #8f93aa; font-size: 10px; letter-spacing: 1px; }"
            "QLabel#toolbarDetail { color: #8e92aa; font-size: 11px; }"
            "QLabel#timeLabel { background: #151722; border: 1px solid #35394f; border-radius: 3px;"
            " color: #d0d4e6; padding: 6px 8px; font-family: Consolas; }"
            "QLabel#selectionLabel { color: #9ea3bd; font-size: 11px; }"
            "QToolButton { color: #c7cadc; border: 1px solid transparent; border-radius: 3px; }"
            "QToolButton:hover { background: #2b2e46; border-color: #464b6c; }"
            "QToolButton:checked { background: #6865e8; color: white; }"
            "QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { background: #191b2a; border: 1px solid #40455f;"
            " border-radius: 3px; padding: 4px 6px; selection-background-color: #6865e8; }"
            "QPushButton { background: #3d4070; border: 1px solid #6865a5; border-radius: 3px; padding: 5px 9px; }"
            "QPushButton:hover { background: #5653a0; }"
            "QScrollBar:vertical, QScrollBar:horizontal { background: #151722; }"
            "QScrollBar::handle { background: #444862; border-radius: 4px; }"
            "QStatusBar { background: #1a1c29; color: #8f93aa; }"
        );
        populate_metadata();
    }

    QTabWidget* build_inspector(QWidget* parent) {
        m_tabs = new QTabWidget(parent);
        m_tabs->addTab(build_notes_tab(false), "Notes");
        m_tabs->addTab(build_phonemes_tab(), "Phonemes");
        m_tabs->addTab(build_automation_tab(), "Automation");
        m_tabs->addTab(build_m7_tab(), "M7 session");
        return m_tabs;
    }

    QLabel* add_info_cell(QHBoxLayout* layout, const QString& title) {
        auto* label = new QLabel(title + ": --", this);
        label->setMinimumWidth(125);
        layout->addWidget(label);
        return label;
    }

    QWidget* build_sidebar() {
        auto* panel = new QWidget(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 8, 0);

        auto* voice_group = new QGroupBox("Voice sources", panel);
        auto* voice_layout = new QVBoxLayout(voice_group);
        m_voice_list = new QListWidget(voice_group);
        m_voice_list->setSelectionMode(QAbstractItemView::SingleSelection);
        voice_layout->addWidget(m_voice_list);
        m_voice_details = new QLabel("Select a voice source.", voice_group);
        m_voice_details->setWordWrap(true);
        m_voice_details->setMinimumHeight(125);
        voice_layout->addWidget(m_voice_details);
        layout->addWidget(voice_group, 1);

        auto* format_group = new QGroupBox("Formats", panel);
        auto* format_layout = new QVBoxLayout(format_group);
        m_format_list = new QListWidget(format_group);
        m_format_list->setMaximumHeight(100);
        format_layout->addWidget(m_format_list);
        layout->addWidget(format_group);

        auto* reload_button = new QPushButton("Reload engine metadata", panel);
        connect(reload_button, &QPushButton::clicked, this, [this] { reload_metadata(); });
        layout->addWidget(reload_button);
        connect(m_voice_list, &QListWidget::currentRowChanged, this,
                [this](int row) { show_voice(row); });
        return panel;
    }

    QWidget* build_editor() {
        auto* panel = new QWidget(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* command_group = new QGroupBox("Score actions", panel);
        auto* command_layout = new QHBoxLayout(command_group);
        m_batch_lyrics = new QLineEdit("ni hao wo", command_group);
        m_batch_lyrics->setPlaceholderText("Lyrics for sequential note fill");
        command_layout->addWidget(m_batch_lyrics, 1);
        auto* lyrics_button = new QPushButton("Apply lyrics", command_group);
        auto* pitch_button = new QPushButton("Write pitch curve", command_group);
        auto* reset_button = new QPushButton("Reset sample", command_group);
        command_layout->addWidget(lyrics_button);
        command_layout->addWidget(pitch_button);
        command_layout->addWidget(reset_button);
        layout->addWidget(command_group);
        connect(lyrics_button, &QPushButton::clicked, this, [this] { apply_batch_lyrics(); });
        connect(pitch_button, &QPushButton::clicked, this, [this] { write_pitch_curve(); });
        connect(reset_button, &QPushButton::clicked, this, [this] { reset_document(); });

        m_roll = new PianoRollWidget(panel);
        m_roll->set_model(m_context, m_score, m_part);
        m_roll->on_changed = [this] { refresh_all(); };
        m_roll->on_selection_changed = [this](int index) { select_note(index); };
        auto* roll_scroll = new QScrollArea(panel);
        roll_scroll->setWidget(m_roll);
        roll_scroll->setWidgetResizable(false);
        roll_scroll->setMinimumHeight(515);
        layout->addWidget(roll_scroll, 1);

        m_tabs = new QTabWidget(panel);
        m_tabs->addTab(build_notes_tab(), "Notes");
        m_tabs->addTab(build_phonemes_tab(), "Phonemes");
        m_tabs->addTab(build_automation_tab(), "Automation");
        m_tabs->addTab(build_m7_tab(), "M7 session");
        m_tabs->setMinimumHeight(235);
        layout->addWidget(m_tabs);
        return panel;
    }

    QWidget* build_notes_tab(bool include_editor_controls = true) {
        auto* tab = new QWidget(this);
        auto* layout = new QVBoxLayout(tab);
        m_note_table = new QTableWidget(tab);
        m_note_table->setColumnCount(5);
        m_note_table->setHorizontalHeaderLabels({"#", "Position", "Duration", "Pitch", "Lyric"});
        m_note_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_note_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_note_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_note_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(m_note_table, 1);

        if (!include_editor_controls) return tab;

        auto* controls = new QHBoxLayout();
        m_selected_lyric = new QLineEdit(tab);
        m_selected_pitch = new QSpinBox(tab);
        m_selected_pitch->setRange(0, 127);
        controls->addWidget(new QLabel("Selected lyric", tab));
        controls->addWidget(m_selected_lyric, 1);
        controls->addWidget(new QLabel("Pitch", tab));
        controls->addWidget(m_selected_pitch);
        auto* apply_button = new QPushButton("Apply selected note", tab);
        controls->addWidget(apply_button);
        layout->addLayout(controls);
        m_selected_note_label = new QLabel("No note selected.", tab);
        layout->addWidget(m_selected_note_label);
        connect(m_note_table, &QTableWidget::itemSelectionChanged, this, [this] {
            if (m_updating_ui || m_note_table->selectedItems().isEmpty()) return;
            select_note(m_note_table->selectedItems().first()->row());
        });
        connect(apply_button, &QPushButton::clicked, this, [this] { apply_selected_note(); });
        return tab;
    }

    QWidget* build_phonemes_tab() {
        auto* tab = new QWidget(this);
        auto* form = new QFormLayout(tab);
        m_phoneme_symbol = new QLineEdit("a", tab);
        m_phoneme_duration = new QDoubleSpinBox(tab);
        m_phoneme_duration->setRange(0, 10);
        m_phoneme_duration->setDecimals(3);
        m_phoneme_duration->setValue(0.16);
        m_body_offset = new QDoubleSpinBox(tab);
        m_body_offset->setRange(-10, 10);
        m_body_offset->setDecimals(3);
        form->addRow("Symbol", m_phoneme_symbol);
        form->addRow("Duration (seconds)", m_phoneme_duration);
        form->addRow("Body offset (seconds)", m_body_offset);
        auto* phoneme_button = new QPushButton("Write body phoneme at slot 0", tab);
        auto* offset_button = new QPushButton("Apply body offset", tab);
        form->addRow(phoneme_button, offset_button);
        m_phoneme_summary = new QLabel("Select a note to inspect pinned phonemes.", tab);
        m_phoneme_summary->setWordWrap(true);
        form->addRow("Readback", m_phoneme_summary);
        connect(phoneme_button, &QPushButton::clicked, this, [this] { apply_phoneme(); });
        connect(offset_button, &QPushButton::clicked, this, [this] { apply_body_offset(); });
        return tab;
    }

    QWidget* build_automation_tab() {
        auto* tab = new QWidget(this);
        auto* layout = new QVBoxLayout(tab);
        m_automation_table = new QTableWidget(tab);
        m_automation_table->setColumnCount(4);
        m_automation_table->setHorizontalHeaderLabels({"Id", "Shape", "Default", "Value at 480"});
        m_automation_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_automation_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        layout->addWidget(m_automation_table, 1);

        auto* automation_controls = new QHBoxLayout();
        m_automation_combo = new QComboBox(tab);
        m_automation_default = new QDoubleSpinBox(tab);
        m_automation_default->setRange(-1000, 1000);
        m_automation_default->setDecimals(3);
        automation_controls->addWidget(new QLabel("Track", tab));
        automation_controls->addWidget(m_automation_combo, 1);
        automation_controls->addWidget(new QLabel("Default", tab));
        automation_controls->addWidget(m_automation_default);
        auto* automation_button = new QPushButton("Write sample points", tab);
        auto* default_button = new QPushButton("Apply default", tab);
        automation_controls->addWidget(automation_button);
        automation_controls->addWidget(default_button);
        layout->addLayout(automation_controls);

        auto* property_controls = new QHBoxLayout();
        m_property_combo = new QComboBox(tab);
        m_property_value = new QLineEdit("0.5", tab);
        auto* property_button = new QPushButton("Write note property", tab);
        property_controls->addWidget(new QLabel("Property", tab));
        property_controls->addWidget(m_property_combo, 1);
        property_controls->addWidget(m_property_value);
        property_controls->addWidget(property_button);
        layout->addLayout(property_controls);
        connect(automation_button, &QPushButton::clicked, this, [this] { write_automation_points(); });
        connect(default_button, &QPushButton::clicked, this, [this] { apply_automation_default(); });
        connect(property_button, &QPushButton::clicked, this, [this] { apply_property(); });
        connect(m_automation_combo, &QComboBox::currentIndexChanged, this,
                [this](int) { refresh_automation_editor(); });
        return tab;
    }

    QWidget* build_m7_tab() {
        auto* tab = new QWidget(this);
        auto* layout = new QVBoxLayout(tab);
        auto* title = new QLabel("M7 synthesis session / audio output", tab);
        title->setStyleSheet("font-size: 16px; font-weight: bold; color: #f4d35e;");
        layout->addWidget(title);
        auto* status = new QLabel(
            "The document, editor, phoneme, automation and plugin panels above are live SVS Core calls.\n\n"
            "M7 session scheduling, synthesized products and WAV output are intentionally marked for manual GUI validation.\n"
            "Do not treat this status panel as an automated M7 pass.", tab);
        status->setWordWrap(true);
        layout->addWidget(status);
        layout->addStretch(1);
        return tab;
    }

    void refresh_all() {
        if (m_context == nullptr) {
            m_engine_status_label->setText("Engine load: context unavailable");
            return;
        }
        refresh_score_info();
        refresh_notes();
        refresh_phonemes();
        refresh_automation();
        refresh_properties();
        m_roll->set_model(m_context, m_score, m_part);
        m_roll->set_selection(m_selected_index);
    }

    void refresh_score_info() {
        svs_score_info info{};
        if (m_score == nullptr || svs_score_get_info(m_score, &info) != SVS_OK) return;
        m_bpm_label->setText("BPM: " + QString::number(info.bpm, 'f', 1));
        m_signature_label->setText("Signature: 4/4");
        m_ppq_label->setText("PPQ: " + QString::number(info.ppq));
        m_length_label->setText("Length: " + QString::number(info.second_count, 'f', 2) + " sec");
        QString engine_text = m_engine_status == SVS_OK ? "Engine load: OK" :
            "Engine load: " + QString::number(static_cast<int>(m_engine_status));
        m_engine_status_label->setText(engine_text);
    }

    void refresh_notes() {
        if (m_note_table == nullptr || m_part == nullptr) return;
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK) return;
        m_updating_ui = true;
        m_note_table->setRowCount(static_cast<int>(note_count));
        for (size_t index = 0; index < note_count; ++index) {
            svs_note_info info{};
            if (svs_note_get_info(notes[index], &info) != SVS_OK) continue;
            m_note_table->setItem(static_cast<int>(index), 0, new QTableWidgetItem(QString::number(index)));
            m_note_table->setItem(static_cast<int>(index), 1, new QTableWidgetItem(QString::number(info.pos, 'f', 0)));
            m_note_table->setItem(static_cast<int>(index), 2, new QTableWidgetItem(QString::number(info.dur, 'f', 0)));
            m_note_table->setItem(static_cast<int>(index), 3, new QTableWidgetItem(QString::number(info.pitch)));
            m_note_table->setItem(static_cast<int>(index), 4, new QTableWidgetItem(text_view(info.lyric)));
        }
        if (m_selected_index >= static_cast<int>(note_count)) m_selected_index = -1;
        if (m_selected_index >= 0) m_note_table->selectRow(m_selected_index);
        m_updating_ui = false;
        refresh_selected_note();
    }

    void refresh_phonemes() {
        if (m_phoneme_summary == nullptr) return;
        const svs_note* note = selected_note();
        if (note == nullptr) {
            m_phoneme_summary->setText("Select a note to inspect pinned phonemes.");
            return;
        }
        svs_phoneme_list phonemes{};
        if (svs_note_get_phonemes(const_cast<svs_note*>(note), &phonemes) != SVS_OK) return;
        QString summary = "count=" + QString::number(static_cast<qulonglong>(phonemes.count)) +
                          ", leading=" + QString::number(static_cast<qulonglong>(phonemes.leading_count)) + ": ";
        for (size_t index = 0; index < phonemes.count; ++index) {
            if (index > 0) summary += " | ";
            summary += text_view(phonemes.items[index].symbol);
        }
        m_phoneme_summary->setText(summary);
        m_body_offset->setValue(svs_note_body_offset(note));
    }

    void refresh_automation() {
        if (m_automation_table == nullptr || m_part == nullptr) return;
        const svs_automation_config* configs = nullptr;
        size_t config_count = 0;
        if (svs_part_get_automation_configs(m_part, &configs, &config_count) != SVS_OK) return;
        m_updating_ui = true;
        const QString previous = m_automation_combo->currentData().toString();
        m_automation_combo->clear();
        m_automation_table->setRowCount(static_cast<int>(config_count));
        for (size_t index = 0; index < config_count; ++index) {
            const QString id = text_view(configs[index].id);
            m_automation_combo->addItem(id, id);
            svs_automation* automation = nullptr;
            double value = NAN;
            if (svs_part_get_automation(m_part, id.toUtf8().constData(), &automation) == SVS_OK) {
                value = svs_automation_evaluate(automation, 480);
            }
            m_automation_table->setItem(static_cast<int>(index), 0, new QTableWidgetItem(id));
            m_automation_table->setItem(static_cast<int>(index), 1,
                                        new QTableWidgetItem(shape_text(configs[index].shape)));
            m_automation_table->setItem(static_cast<int>(index), 2,
                                        new QTableWidgetItem(QString::number(configs[index].default_value, 'f', 3)));
            m_automation_table->setItem(static_cast<int>(index), 3,
                                        new QTableWidgetItem(std::isnan(value) ? "NaN" : QString::number(value, 'f', 3)));
        }
        const int previous_index = m_automation_combo->findData(previous);
        if (previous_index >= 0) m_automation_combo->setCurrentIndex(previous_index);
        m_updating_ui = false;
        refresh_automation_editor();
    }

    void refresh_automation_editor() {
        if (m_automation_combo == nullptr || m_updating_ui || m_part == nullptr) return;
        const QByteArray id = m_automation_combo->currentData().toString().toUtf8();
        svs_automation* automation = nullptr;
        if (svs_part_get_automation(m_part, id.constData(), &automation) == SVS_OK) {
            const double value = svs_automation_default_value(automation);
            if (std::isfinite(value)) m_automation_default->setValue(value);
        }
    }

    void refresh_properties() {
        if (m_property_combo == nullptr || m_part == nullptr) return;
        const svs_property_config* configs = nullptr;
        size_t config_count = 0;
        if (svs_part_get_note_property_configs(m_part, &configs, &config_count) != SVS_OK) return;
        const QString previous = m_property_combo->currentData().toString();
        m_property_combo->clear();
        for (size_t index = 0; index < config_count; ++index) {
            const QString id = text_view(configs[index].id);
            m_property_combo->addItem(id + " (" + text_view(configs[index].display_name) + ")", id);
        }
        const int previous_index = m_property_combo->findData(previous);
        if (previous_index >= 0) m_property_combo->setCurrentIndex(previous_index);
    }

    void refresh_selected_note() {
        const svs_note* note = selected_note();
        if (note == nullptr) {
            m_selected_note_label->setText("No note selected.");
            m_selected_lyric->clear();
            return;
        }
        svs_note_info info{};
        if (svs_note_get_info(note, &info) != SVS_OK) return;
        m_selected_note_label->setText(
            "Selected note: pos=" + QString::number(info.pos, 'f', 0) +
            ", dur=" + QString::number(info.dur, 'f', 0) +
            ", revision=" + QString::number(static_cast<qulonglong>(info.revision)));
        m_selected_lyric->setText(text_view(info.lyric));
        m_selected_pitch->setValue(info.pitch);
    }

    svs_note* selected_note() const {
        if (m_part == nullptr || m_selected_index < 0) return nullptr;
        const svs_note* const* notes = nullptr;
        size_t note_count = 0;
        if (svs_part_get_notes(m_part, &notes, &note_count) != SVS_OK ||
            static_cast<size_t>(m_selected_index) >= note_count) return nullptr;
        return const_cast<svs_note*>(notes[m_selected_index]);
    }

    void select_note(int index) {
        m_selected_index = index;
        if (m_roll != nullptr) m_roll->set_selection(index);
        if (m_note_table != nullptr && index >= 0 && index < m_note_table->rowCount()) {
            m_updating_ui = true;
            m_note_table->selectRow(index);
            m_updating_ui = false;
        }
        refresh_selected_note();
        refresh_phonemes();
    }

    void reload_metadata() {
        if (m_context == nullptr) return;
        m_engine_status = svs_context_load_engines(m_context);
        populate_metadata();
        refresh_score_info();
        statusBar()->showMessage("Engine metadata reloaded.");
    }

    void populate_metadata() {
        if (m_context == nullptr) return;
        if (m_voice_list != nullptr) m_voice_list->clear();
        if (m_voice_combo != nullptr) m_voice_combo->clear();
        const svs_voice_source_info* voices = nullptr;
        size_t voice_count = 0;
        if (svs_context_get_voice_sources(m_context, &voices, &voice_count) == SVS_OK) {
            for (size_t index = 0; index < voice_count; ++index) {
                const QString name = text_view(voices[index].name);
                const QString description = text_view(voices[index].description);
                if (m_voice_list != nullptr) {
                    auto* item = new QListWidgetItem(name, m_voice_list);
                    item->setToolTip(description);
                }
                if (m_voice_combo != nullptr) {
                    m_voice_combo->addItem(name, static_cast<int>(index));
                    m_voice_combo->setItemData(static_cast<int>(index), description, Qt::ToolTipRole);
                }
            }
        }
        if (m_format_list != nullptr) m_format_list->clear();
        const svs_format_info* formats = nullptr;
        size_t format_count = 0;
        if (svs_context_get_formats(m_context, &formats, &format_count) == SVS_OK) {
            for (size_t index = 0; index < format_count; ++index) {
                const QString format = text_view(formats[index].name) + " (." +
                    text_view(formats[index].extension) + ")";
                if (m_format_list != nullptr) m_format_list->addItem(format);
            }
        }
        if (m_format_summary != nullptr) {
            m_format_summary->setText("Formats: " + QString::number(static_cast<qulonglong>(format_count)));
        }
        if (m_voice_list != nullptr && m_voice_list->count() > 0) m_voice_list->setCurrentRow(0);
        if (m_voice_combo != nullptr && m_voice_combo->count() > 0) {
            m_voice_combo->setCurrentIndex(0);
            show_voice(0);
        }
    }

    void show_voice(int row) {
        if (m_context == nullptr || row < 0) return;
        const svs_voice_source_info* voices = nullptr;
        size_t voice_count = 0;
        if (svs_context_get_voice_sources(m_context, &voices, &voice_count) != SVS_OK ||
            static_cast<size_t>(row) >= voice_count) return;
        const auto& voice = voices[row];
        const QString details =
            "Id: " + text_view(voice.id) + "\n" +
            text_view(voice.description) + "\n\nAvatar: " + image_summary(voice.avatar) +
            "\nPortrait: " + image_summary(voice.portrait);
        if (m_voice_details != nullptr) {
            m_voice_details->setText(text_view(voice.id));
            m_voice_details->setToolTip(details);
        }
        if (m_voice_list != nullptr && row < m_voice_list->count()) {
            m_voice_list->setCurrentRow(row);
        }
    }

    void apply_batch_lyrics() {
        if (m_part == nullptr) return;
        const QByteArray lyrics = m_batch_lyrics->text().toUtf8();
        const svs_status status = svs_part_apply_lyrics_batch(m_part, 0, lyrics.constData());
        statusBar()->showMessage(status == SVS_OK ? "Lyrics applied through SVS Core G2P." : "Lyrics write failed.");
        refresh_all();
    }

    void write_pitch_curve() {
        if (m_part == nullptr) return;
        const svs_pitch_point points[] = {{0, 60}, {480, 62}, {960, 65}, {1680, 67}, {2640, 69}, {3120, 67}};
        const svs_pitch_segment segments[] = {{points, 6}};
        const svs_status status = svs_part_pitch_set_segments(m_part, segments, 1);
        statusBar()->showMessage(status == SVS_OK ? "Pitch curve written." : "Pitch curve write failed.");
        refresh_all();
    }

    void reset_document() {
        if (m_score != nullptr) svs_score_destroy(m_score);
        m_score = nullptr;
        m_track = nullptr;
        m_part = nullptr;
        if (m_context == nullptr || svs_score_create(m_context, &m_score) != SVS_OK ||
            svs_score_create_track(m_score, &m_track) != SVS_OK ||
            svs_track_create_part(m_track, &m_part) != SVS_OK) return;
        m_selected_index = -1;
        seed_document();
        m_roll->set_model(m_context, m_score, m_part);
        refresh_all();
    }

    void apply_selected_note() {
        svs_note* note = selected_note();
        if (note == nullptr) return;
        const QByteArray lyric = m_selected_lyric->text().toUtf8();
        svs_note_set_lyric(note, lyric.constData());
        svs_note_set_pitch(note, m_selected_pitch->value());
        statusBar()->showMessage("Selected note updated through SVS Core.");
        refresh_all();
    }

    void apply_phoneme() {
        svs_note* note = selected_note();
        if (note == nullptr) return;
        const QByteArray symbol = m_phoneme_symbol->text().toUtf8();
        const svs_phoneme phoneme = {{symbol.constData(), static_cast<size_t>(symbol.size())},
                                     m_phoneme_duration->value(), 1};
        const svs_status status = svs_note_phoneme_set(note, 0, &phoneme);
        statusBar()->showMessage(status == SVS_OK ? "Body phoneme updated." : "Phoneme update failed.");
        refresh_all();
    }

    void apply_body_offset() {
        svs_note* note = selected_note();
        if (note == nullptr) return;
        svs_note_set_body_offset(note, m_body_offset->value());
        refresh_all();
    }

    void write_automation_points() {
        if (m_part == nullptr) return;
        const QByteArray id = m_automation_combo->currentData().toString().toUtf8();
        svs_automation* automation = nullptr;
        if (svs_part_get_automation(m_part, id.constData(), &automation) != SVS_OK) return;
        const svs_automation_config* configs = nullptr;
        size_t config_count = 0;
        svs_part_get_automation_configs(m_part, &configs, &config_count);
        svs_automation_shape shape = SVS_AUTOMATION_CONTINUOUS;
        for (size_t index = 0; index < config_count; ++index) {
            if (text_view(configs[index].id) == m_automation_combo->currentData().toString()) shape = configs[index].shape;
        }
        const svs_pitch_point points[] = {{0, -0.15}, {480, 0.2}, {960, 0.05}};
        const svs_pitch_segment segments[] = {{points, 3}};
        const svs_status status = shape == SVS_AUTOMATION_PIECEWISE
            ? svs_automation_set_segments(automation, segments, 1)
            : svs_automation_set_points(automation, points, 3);
        statusBar()->showMessage(status == SVS_OK ? "Automation points written." : "Automation write failed.");
        refresh_all();
    }

    void apply_automation_default() {
        const QByteArray id = m_automation_combo->currentData().toString().toUtf8();
        svs_automation* automation = nullptr;
        if (m_part == nullptr || svs_part_get_automation(m_part, id.constData(), &automation) != SVS_OK) return;
        svs_automation_set_default_value(automation, m_automation_default->value());
        refresh_all();
    }

    void apply_property() {
        svs_note* note = selected_note();
        if (note == nullptr) return;
        const QByteArray id = m_property_combo->currentData().toString().toUtf8();
        const QByteArray value = m_property_value->text().toUtf8();
        const svs_property_config* configs = nullptr;
        size_t config_count = 0;
        if (m_part == nullptr || svs_part_get_note_property_configs(m_part, &configs, &config_count) != SVS_OK) return;
        svs_property_kind kind = SVS_PROPERTY_NUMBER;
        for (size_t index = 0; index < config_count; ++index) {
            if (text_view(configs[index].id) == m_property_combo->currentData().toString()) kind = configs[index].kind;
        }
        svs_status status = SVS_ERR_INVALID_ARG;
        if (kind == SVS_PROPERTY_TEXT) {
            status = svs_note_property_set_string(note, id.constData(), value.constData());
        } else {
            bool ok = false;
            const double number = m_property_value->text().toDouble(&ok);
            if (ok) status = svs_note_property_set_double(note, id.constData(), number);
        }
        statusBar()->showMessage(status == SVS_OK ? "Note property written." : "Note property write failed.");
        refresh_all();
    }

    svs_context* m_context = nullptr;
    svs_score* m_score = nullptr;
    svs_track* m_track = nullptr;
    svs_part* m_part = nullptr;
    svs_status m_engine_status = SVS_OK;
    int m_selected_index = -1;
    bool m_updating_ui = false;

    QLabel* m_bpm_label = nullptr;
    QLabel* m_signature_label = nullptr;
    QLabel* m_ppq_label = nullptr;
    QLabel* m_length_label = nullptr;
    QLabel* m_engine_status_label = nullptr;
    QLabel* m_format_summary = nullptr;
    QLabel* m_time_label = nullptr;
    QToolButton* m_inspector_button = nullptr;
    QComboBox* m_voice_combo = nullptr;
    QListWidget* m_voice_list = nullptr;
    QLabel* m_voice_details = nullptr;
    QListWidget* m_format_list = nullptr;
    PianoRollWidget* m_roll = nullptr;
    QDockWidget* m_inspector_dock = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_note_table = nullptr;
    QLabel* m_selected_note_label = nullptr;
    QLineEdit* m_selected_lyric = nullptr;
    QSpinBox* m_selected_pitch = nullptr;
    QLineEdit* m_batch_lyrics = nullptr;
    QLineEdit* m_phoneme_symbol = nullptr;
    QDoubleSpinBox* m_phoneme_duration = nullptr;
    QDoubleSpinBox* m_body_offset = nullptr;
    QLabel* m_phoneme_summary = nullptr;
    QTableWidget* m_automation_table = nullptr;
    QComboBox* m_automation_combo = nullptr;
    QDoubleSpinBox* m_automation_default = nullptr;
    QComboBox* m_property_combo = nullptr;
    QLineEdit* m_property_value = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("SVS Core Qt Piano Roll");
    MainWindow window;
    window.show();
    return application.exec();
}