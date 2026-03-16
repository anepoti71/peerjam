#pragma once

#include "peerjam/logic/probe/measurement.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace peerjam {

struct AudioOptions {
    int seconds {};
    int sample_rate_hz {};
    int frame_samples {};
    int channels {};
    int source_buffer_ms {};
    int sink_buffer_ms {};
    int packetsize {};
    int resend_buffer_ms {};
    int resend_limit {};
    int resend_interval_ms {};
    int resend_max_frames {};
    int redundancy {};
    bool dynamic_resampling {true};
    std::string codec {"pcm"};
    bool device_io_enabled {false};
    int input_device_index {-1};
    int output_device_index {-1};
    double tone_hz {};
    double tone_level {};
    int source_id {1};
    int sink_id {1};
};

struct AudioStreamReport {
    bool enabled {};
    bool source_active {};
    std::uint32_t rendered_blocks {};
    std::uint32_t active_blocks {};
    std::uint32_t play_events {};
    std::uint32_t stop_events {};
    std::uint32_t lost_blocks {};
    std::uint32_t reordered_blocks {};
    std::uint32_t resent_blocks {};
    std::uint32_t gap_blocks {};
    double drift_ppm_estimate {};
    std::string codec_name;
    SummaryStats output_rms;
    SummaryStats output_peak;
    SummaryStats buffer_fill_ratio;

    std::string to_json() const;
};

class AudioStreamTracker {
  public:
    explicit AudioStreamTracker(const AudioOptions& options);
    void set_options(const AudioOptions& options) { options_ = options; }

    void on_render_block(const float* interleaved, int frames, int channels, std::uint64_t steady_now_ns);
    void on_buffer_fill(float ratio, std::uint64_t steady_now_ns);
    void on_play_event();
    void on_stop_event();
    void on_lost_blocks(std::uint32_t count);
    void on_reordered_blocks(std::uint32_t count);
    void on_resent_blocks(std::uint32_t count);
    void on_gap_blocks(std::uint32_t count);
    double current_drift_ppm_estimate() const;
    std::uint32_t current_rendered_blocks() const { return rendered_blocks_; }
    std::uint32_t current_lost_blocks() const { return lost_blocks_; }
    std::uint32_t current_gap_blocks() const { return gap_blocks_; }
    AudioStreamReport finish() const;

  private:
    static SummaryStats summarize(const std::vector<double>& values);

    AudioOptions options_;
    std::vector<double> output_rms_;
    std::vector<double> output_peak_;
    std::vector<double> buffer_fill_ratio_;
    std::deque<double> drift_time_s_;
    std::deque<double> drift_ratio_;
    double drift_sum_x_ {};
    double drift_sum_y_ {};
    double drift_sum_xx_ {};
    double drift_sum_xy_ {};
    std::uint32_t rendered_blocks_ {};
    std::uint32_t active_blocks_ {};
    std::uint32_t play_events_ {};
    std::uint32_t stop_events_ {};
    std::uint32_t lost_blocks_ {};
    std::uint32_t reordered_blocks_ {};
    std::uint32_t resent_blocks_ {};
    std::uint32_t gap_blocks_ {};
};

}  // namespace peerjam
