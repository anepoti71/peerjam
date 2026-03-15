#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace peerjam {

struct SummaryStats {
    std::size_t count {};
    double minimum_ms {};
    double average_ms {};
    double p95_ms {};
    double maximum_ms {};
};

struct Recommendation {
    std::string status;
    std::string codec;
    int sample_rate_hz {};
    int frame_samples {};
    double initial_buffer_ms {};
    std::string rationale;
};

struct MeasurementReport {
    bool direct_path_established {};
    SummaryStats rtt_ms;
    SummaryStats one_way_ms;
    SummaryStats ingress_interval_ms;
    SummaryStats ingress_jitter_ms;
    SummaryStats echo_jitter_ms;
    std::uint32_t sent_packets {};
    std::uint32_t echoed_packets {};
    std::uint32_t ingress_packets {};
    std::uint32_t reordered_packets {};
    std::uint32_t echo_reordered_packets {};
    std::uint32_t missing_packets {};
    double loss_percent {};
    Recommendation recommendation;

    std::string to_json() const;
};

class MeasurementTracker {
  public:
    explicit MeasurementTracker(std::uint32_t expected_packets);

    void mark_sent(std::uint32_t sequence, std::uint64_t sent_at_ns);
    void on_probe_received(std::uint32_t sequence, std::uint64_t arrival_ns);
    void on_echo_received(std::uint32_t sequence, std::uint64_t arrival_ns);
    void set_direct_path_established(bool value);
    MeasurementReport finish() const;

  private:
    static SummaryStats summarize(const std::vector<double>& values_ms);
    Recommendation recommend(const MeasurementReport& report) const;

    std::uint32_t expected_packets_ {};
    bool direct_path_established_ {false};
    std::unordered_map<std::uint32_t, std::uint64_t> sent_at_ns_;
    std::vector<double> rtt_ms_;
    std::vector<double> ingress_interval_ms_;
    std::vector<double> ingress_jitter_ms_;
    std::vector<double> echo_jitter_ms_;
    std::optional<std::uint64_t> last_probe_arrival_ns_;
    std::optional<double> last_ingress_interval_ms_;
    std::optional<double> last_rtt_ms_;
    std::optional<std::uint32_t> last_probe_sequence_;
    std::optional<std::uint32_t> last_echo_sequence_;
    std::uint32_t ingress_packets_ {};
    std::uint32_t echoed_packets_ {};
    std::uint32_t reordered_packets_ {};
    std::uint32_t echo_reordered_packets_ {};
    std::uint32_t missing_packets_ {};
};

}  // namespace peerjam
