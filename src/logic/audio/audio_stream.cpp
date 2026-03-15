#include "peerjam/common/stats_utils.hpp"
#include "peerjam/logic/audio/audio_stream.hpp"

#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace peerjam {
namespace {

double estimate_drift_ppm(const std::vector<double>& time_s, const std::vector<double>& ratio, int sink_buffer_ms) {
    if (time_s.size() < 2 || ratio.size() != time_s.size() || sink_buffer_ms <= 0) {
        return 0.0;
    }
    const double n = static_cast<double>(time_s.size());
    const double sum_x = std::accumulate(time_s.begin(), time_s.end(), 0.0);
    const double sum_y = std::accumulate(ratio.begin(), ratio.end(), 0.0);
    double sum_xy = 0.0;
    double sum_xx = 0.0;
    for (std::size_t i = 0; i < time_s.size(); ++i) {
        sum_xy += time_s[i] * ratio[i];
        sum_xx += time_s[i] * time_s[i];
    }
    const double denominator = (n * sum_xx) - (sum_x * sum_x);
    if (std::abs(denominator) < 1e-9) {
        return 0.0;
    }
    const double slope_ratio_per_second = ((n * sum_xy) - (sum_x * sum_y)) / denominator;
    return slope_ratio_per_second * (static_cast<double>(sink_buffer_ms) / 1000.0) * 1'000'000.0;
}

}  // namespace

AudioStreamTracker::AudioStreamTracker(const AudioOptions& options)
    : options_(options) {}

void AudioStreamTracker::on_render_block(const float* interleaved, int frames, int channels, std::uint64_t) {
    if (!interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    ++rendered_blocks_;
    const int samples = frames * channels;
    double sum_sq = 0.0;
    double peak = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double value = interleaved[i];
        sum_sq += value * value;
        peak = std::max(peak, std::abs(value));
    }
    const double rms = std::sqrt(sum_sq / static_cast<double>(samples));
    output_rms_.push_back(rms);
    output_peak_.push_back(peak);
    if (rms > 1e-4 || peak > 1e-3) {
        ++active_blocks_;
    }
}

void AudioStreamTracker::on_buffer_fill(float ratio, std::uint64_t steady_now_ns) {
    buffer_fill_ratio_.push_back(std::clamp(static_cast<double>(ratio), 0.0, 1.5));
    buffer_fill_time_s_.push_back(static_cast<double>(steady_now_ns) / 1'000'000'000.0);
}

void AudioStreamTracker::on_play_event() { ++play_events_; }
void AudioStreamTracker::on_stop_event() { ++stop_events_; }
void AudioStreamTracker::on_lost_blocks(std::uint32_t count) { lost_blocks_ += count; }
void AudioStreamTracker::on_reordered_blocks(std::uint32_t count) { reordered_blocks_ += count; }
void AudioStreamTracker::on_resent_blocks(std::uint32_t count) { resent_blocks_ += count; }
void AudioStreamTracker::on_gap_blocks(std::uint32_t count) { gap_blocks_ += count; }

SummaryStats AudioStreamTracker::summarize(const std::vector<double>& values) {
    if (values.empty()) {
        return {};
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return SummaryStats {
        .count = values.size(),
        .minimum_ms = *std::min_element(values.begin(), values.end()),
        .average_ms = sum / static_cast<double>(values.size()),
        .p95_ms = percentile(values, 95.0),
        .maximum_ms = *std::max_element(values.begin(), values.end()),
    };
}

AudioStreamReport AudioStreamTracker::finish() const {
    AudioStreamReport report;
    report.enabled = options_.seconds > 0;
    report.source_active = active_blocks_ > 0 || play_events_ > 0;
    report.rendered_blocks = rendered_blocks_;
    report.active_blocks = active_blocks_;
    report.play_events = play_events_;
    report.stop_events = stop_events_;
    report.lost_blocks = lost_blocks_;
    report.reordered_blocks = reordered_blocks_;
    report.resent_blocks = resent_blocks_;
    report.gap_blocks = gap_blocks_;
    report.output_rms = summarize(output_rms_);
    report.output_peak = summarize(output_peak_);
    report.buffer_fill_ratio = summarize(buffer_fill_ratio_);
    report.drift_ppm_estimate = estimate_drift_ppm(buffer_fill_time_s_, buffer_fill_ratio_, options_.sink_buffer_ms);
    return report;
}

std::string AudioStreamReport::to_json() const {
    auto fmt_stats = [](const SummaryStats& stats) {
        std::ostringstream os;
        os << "{"
           << "\"count\":" << stats.count << ","
           << "\"minimum\":" << std::fixed << std::setprecision(6) << stats.minimum_ms << ","
           << "\"average\":" << stats.average_ms << ","
           << "\"p95\":" << stats.p95_ms << ","
           << "\"maximum\":" << stats.maximum_ms
           << "}";
        return os.str();
    };

    std::ostringstream os;
    os << "{\n"
       << "  \"enabled\": " << (enabled ? "true" : "false") << ",\n"
       << "  \"source_active\": " << (source_active ? "true" : "false") << ",\n"
       << "  \"rendered_blocks\": " << rendered_blocks << ",\n"
       << "  \"active_blocks\": " << active_blocks << ",\n"
       << "  \"play_events\": " << play_events << ",\n"
       << "  \"stop_events\": " << stop_events << ",\n"
       << "  \"lost_blocks\": " << lost_blocks << ",\n"
       << "  \"reordered_blocks\": " << reordered_blocks << ",\n"
       << "  \"resent_blocks\": " << resent_blocks << ",\n"
       << "  \"gap_blocks\": " << gap_blocks << ",\n"
       << "  \"buffer_fill_ratio\": " << fmt_stats(buffer_fill_ratio) << ",\n"
       << "  \"output_rms\": " << fmt_stats(output_rms) << ",\n"
       << "  \"output_peak\": " << fmt_stats(output_peak) << ",\n"
       << "  \"drift_ppm_estimate\": " << std::fixed << std::setprecision(3) << drift_ppm_estimate << "\n"
       << "}";
    return os.str();
}

}  // namespace peerjam
