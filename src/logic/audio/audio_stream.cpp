#include "peerjam/common/stats_utils.hpp"
#include "peerjam/common/json_utils.hpp"
#include "peerjam/logic/audio/audio_stream.hpp"

#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace peerjam {
namespace {

double estimate_drift_ppm(std::size_t n, double sum_x, double sum_y, double sum_xx, double sum_xy, int sink_buffer_ms) {
    if (n < 2 || sink_buffer_ms <= 0) {
        return 0.0;
    }
    const double n_d = static_cast<double>(n);
    const double denominator = (n_d * sum_xx) - (sum_x * sum_x);
    if (std::abs(denominator) < 1e-9) {
        return 0.0;
    }
    const double slope_ratio_per_second = ((n_d * sum_xy) - (sum_x * sum_y)) / denominator;
    return slope_ratio_per_second * (static_cast<double>(sink_buffer_ms) / 1000.0) * 1'000'000.0;
}

}  // namespace

namespace {
std::size_t estimated_block_count(const AudioOptions& options) {
    if (options.seconds <= 0 || options.sample_rate_hz <= 0 || options.frame_samples <= 0) {
        return 0;
    }
    const double blocks = (static_cast<double>(options.seconds) * static_cast<double>(options.sample_rate_hz))
        / static_cast<double>(options.frame_samples);
    return static_cast<std::size_t>(std::max(1.0, std::ceil(blocks)));
}
}

AudioStreamTracker::AudioStreamTracker(const AudioOptions& options)
    : options_(options) {
    const auto reserve_n = estimated_block_count(options_);
    output_rms_.reserve(reserve_n);
    output_peak_.reserve(reserve_n);
    buffer_fill_ratio_.reserve(reserve_n);
}

void AudioStreamTracker::set_options(const AudioOptions& options) {
    options_ = options;
    const auto reserve_n = estimated_block_count(options_);
    if (reserve_n > output_rms_.capacity()) {
        output_rms_.reserve(reserve_n);
    }
    if (reserve_n > output_peak_.capacity()) {
        output_peak_.reserve(reserve_n);
    }
    if (reserve_n > buffer_fill_ratio_.capacity()) {
        buffer_fill_ratio_.reserve(reserve_n);
    }
}

void AudioStreamTracker::on_render_block(const float* interleaved, int frames, int channels, std::uint64_t) {
    if (!interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    ++rendered_blocks_;
    const int samples = frames * channels;
    float sum_sq = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const float value = interleaved[i];
        sum_sq += value * value;
        peak = std::max(peak, std::abs(value));
    }
    const float rms = std::sqrt(sum_sq / static_cast<float>(samples));
    output_rms_.push_back(rms);
    output_peak_.push_back(peak);
    if (rms > 1e-4f || peak > 1e-3f) {
        ++active_blocks_;
    }
}

void AudioStreamTracker::on_buffer_fill(float ratio, std::uint64_t steady_now_ns) {
    constexpr std::size_t kDriftWindowSamples = 300;
    const float clamped_ratio = std::clamp(ratio, 0.0f, 1.5f);
    const double time_s = static_cast<double>(steady_now_ns) / 1'000'000'000.0;
    buffer_fill_ratio_.push_back(clamped_ratio);

    if (drift_samples_.size() >= kDriftWindowSamples) {
        const auto [old_x, old_y] = drift_samples_.front();
        drift_samples_.pop_front();
        drift_sum_x_ -= old_x;
        drift_sum_y_ -= old_y;
        drift_sum_xx_ -= old_x * old_x;
        drift_sum_xy_ -= old_x * old_y;
    }

    drift_samples_.emplace_back(time_s, static_cast<double>(clamped_ratio));
    drift_sum_x_ += time_s;
    drift_sum_y_ += clamped_ratio;
    drift_sum_xx_ += time_s * time_s;
    drift_sum_xy_ += time_s * clamped_ratio;
}

void AudioStreamTracker::on_play_event() { ++play_events_; }
void AudioStreamTracker::on_stop_event() { ++stop_events_; }
void AudioStreamTracker::on_lost_blocks(std::uint32_t count) { lost_blocks_ += count; }
void AudioStreamTracker::on_reordered_blocks(std::uint32_t count) { reordered_blocks_ += count; }
void AudioStreamTracker::on_resent_blocks(std::uint32_t count) { resent_blocks_ += count; }
void AudioStreamTracker::on_gap_blocks(std::uint32_t count) { gap_blocks_ += count; }

double AudioStreamTracker::current_drift_ppm_estimate() const {
    return estimate_drift_ppm(drift_samples_.size(), drift_sum_x_, drift_sum_y_, drift_sum_xx_, drift_sum_xy_, options_.sink_buffer_ms);
}

SummaryStats AudioStreamTracker::summarize(const std::vector<float>& values) {
    if (values.empty()) {
        return {};
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return SummaryStats {
        .count = values.size(),
        .minimum_ms = static_cast<double>(*std::min_element(values.begin(), values.end())),
        .average_ms = sum / static_cast<double>(values.size()),
        .p95_ms = percentile(values, 95.0),
        .maximum_ms = static_cast<double>(*std::max_element(values.begin(), values.end())),
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
    report.drift_ppm_estimate = current_drift_ppm_estimate();
    report.codec_name = options_.codec;
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
       << "  \"codec\": " << json_quote(codec_name) << ",\n"
       << "  \"buffer_fill_ratio\": " << fmt_stats(buffer_fill_ratio) << ",\n"
       << "  \"output_rms\": " << fmt_stats(output_rms) << ",\n"
       << "  \"output_peak\": " << fmt_stats(output_peak) << ",\n"
       << "  \"drift_ppm_estimate\": " << std::fixed << std::setprecision(3) << drift_ppm_estimate << "\n"
       << "}";
    return os.str();
}

}  // namespace peerjam
