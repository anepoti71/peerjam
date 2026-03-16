#pragma once

#include "peerjam/logic/control/peer_types.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace peerjam {

struct PredictorTelemetrySample {
    std::uint64_t steady_time_ns {};
    float buffer_fill_ratio {};
    std::uint32_t lost_blocks {};
    std::uint32_t gap_blocks {};
    double drift_ppm {};
    double selected_latency_ms {};
    double selected_jitter_ms {};
};

enum class PredictorMitigation : int {
    None = 0,
    RaiseSinkBuffer = 1,
    RaiseSinkBufferLarge = 2,
    SwitchCodecOpusLowDelay = 3,
};

struct PredictorDecision {
    std::uint64_t sequence {};
    std::uint64_t source_time_ns {};
    double risk_score {};
    PredictorMitigation mitigation {PredictorMitigation::None};
};

class PredictorWorker {
  public:
    explicit PredictorWorker(PredictorOptions options);
    ~PredictorWorker();

    PredictorWorker(const PredictorWorker&) = delete;
    PredictorWorker& operator=(const PredictorWorker&) = delete;

    void start();
    void stop();
    bool try_submit(const PredictorTelemetrySample& sample);
    PredictorDecision latest_decision() const;

    std::uint64_t dropped_samples() const { return dropped_samples_.load(); }
    bool enabled() const { return options_.enabled; }
    bool shadow_mode() const { return options_.shadow_mode; }

  private:
    static constexpr std::size_t kQueueCapacity = 256;

    void loop();
    bool pop(PredictorTelemetrySample& sample);
    PredictorDecision evaluate(
        const PredictorTelemetrySample& current,
        double smoothed_lost_rate,
        double smoothed_gap_rate,
        std::uint64_t sequence
    ) const;

    PredictorOptions options_;
    std::array<PredictorTelemetrySample, kQueueCapacity> queue_ {};
    std::atomic<std::size_t> write_seq_ {0};
    std::atomic<std::size_t> read_seq_ {0};
    std::atomic<std::uint64_t> dropped_samples_ {0};
    std::atomic<bool> running_ {false};
    std::thread worker_;

    std::atomic<std::uint64_t> decision_sequence_ {0};
    std::atomic<std::uint64_t> decision_time_ns_ {0};
    std::atomic<double> decision_risk_score_ {0.0};
    std::atomic<int> decision_mitigation_ {static_cast<int>(PredictorMitigation::None)};
    std::atomic<std::uint64_t> decision_version_ {0};
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    double ewma_lost_rate_ {0.0};
    double ewma_gap_rate_ {0.0};
    bool ewma_rates_initialized_ {false};
};

}  // namespace peerjam
