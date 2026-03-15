#pragma once

#include "peerjam/logic/probe/measurement.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace peerjam {

struct AudioOptions {
    int seconds {0};
    int sample_rate_hz {48000};
    int frame_samples {128};
    int channels {2};
    int source_buffer_ms {3};
    int sink_buffer_ms {5};
    int packetsize {512};
    int resend_buffer_ms {30};
    int resend_limit {3};
    int resend_interval_ms {3};
    int resend_max_frames {4};
    int redundancy {1};
    bool dynamic_resampling {true};
    double tone_hz {440.0};
    double tone_level {0.15};
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
    SummaryStats output_rms;
    SummaryStats output_peak;
    SummaryStats buffer_fill_ratio;

    std::string to_json() const;
};

class AudioStreamTracker {
  public:
    explicit AudioStreamTracker(const AudioOptions& options);

    void on_render_block(const float* interleaved, int frames, int channels, std::uint64_t steady_now_ns);
    void on_buffer_fill(float ratio, std::uint64_t steady_now_ns);
    void on_play_event();
    void on_stop_event();
    void on_lost_blocks(std::uint32_t count);
    void on_reordered_blocks(std::uint32_t count);
    void on_resent_blocks(std::uint32_t count);
    void on_gap_blocks(std::uint32_t count);
    AudioStreamReport finish() const;

  private:
    static SummaryStats summarize(const std::vector<double>& values);

    AudioOptions options_;
    std::vector<double> output_rms_;
    std::vector<double> output_peak_;
    std::vector<double> buffer_fill_ratio_;
    std::vector<double> buffer_fill_time_s_;
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
