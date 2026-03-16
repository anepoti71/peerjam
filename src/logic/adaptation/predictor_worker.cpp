#include "peerjam/logic/adaptation/predictor_worker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace peerjam {
namespace {

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

}

PredictorWorker::PredictorWorker(PredictorOptions options)
    : options_(options) {
    if (options_.interval_ms < 20) {
        options_.interval_ms = 20;
    }
    if (options_.risk_medium_threshold < 0.05) {
        options_.risk_medium_threshold = 0.05;
    }
    if (options_.risk_high_threshold < options_.risk_medium_threshold + 0.05) {
        options_.risk_high_threshold = options_.risk_medium_threshold + 0.05;
    }
    if (options_.apply_cooldown_ms < 200) {
        options_.apply_cooldown_ms = 200;
    }
    if (options_.max_sink_buffer_ms < 5) {
        options_.max_sink_buffer_ms = 5;
    }
}

PredictorWorker::~PredictorWorker() {
    stop();
}

void PredictorWorker::start() {
    if (!options_.enabled) {
        return;
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread([this]() { loop(); });
}

void PredictorWorker::stop() {
    bool was_running = running_.exchange(false);
    wake_cv_.notify_one();
    if (was_running && worker_.joinable()) {
        worker_.join();
    }
}

bool PredictorWorker::try_submit(const PredictorTelemetrySample& sample) {
    if (!options_.enabled) {
        return false;
    }
    const std::size_t write = write_seq_.load(std::memory_order_relaxed);
    const std::size_t read = read_seq_.load(std::memory_order_acquire);
    if ((write - read) >= kQueueCapacity) {
        dropped_samples_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    queue_[write % kQueueCapacity] = sample;
    write_seq_.store(write + 1, std::memory_order_release);
    wake_cv_.notify_one();
    return true;
}

PredictorDecision PredictorWorker::latest_decision() const {
    PredictorDecision decision;
    // Seqlock-style snapshot across multiple atomics:
    // writer publishes odd version -> payload fields -> even version.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t version_before = decision_version_.load(std::memory_order_acquire);
        if ((version_before & 1ULL) != 0ULL) {
            continue;
        }
        decision.sequence = decision_sequence_.load(std::memory_order_relaxed);
        decision.source_time_ns = decision_time_ns_.load(std::memory_order_relaxed);
        decision.risk_score = decision_risk_score_.load(std::memory_order_relaxed);
        decision.mitigation = static_cast<PredictorMitigation>(decision_mitigation_.load(std::memory_order_relaxed));
        const std::uint64_t version_after = decision_version_.load(std::memory_order_acquire);
        if (version_before == version_after && (version_after & 1ULL) == 0ULL) {
            return decision;
        }
    }
    // All 3 seqlock attempts failed under contention; return an empty decision rather
    // than a potentially torn snapshot.  Caller already handles sequence==0 as "no data".
    return {};
}

void PredictorWorker::loop() {
    PredictorTelemetrySample previous {};
    bool has_previous = false;
    std::uint64_t sequence = 0;

    while (running_.load(std::memory_order_relaxed)) {
        PredictorTelemetrySample sample {};
        bool saw_sample = false;
        while (pop(sample)) {
            saw_sample = true;
            if (!has_previous) {
                previous = sample;
                has_previous = true;
                continue;
            }
            const std::uint32_t delta_lost = sample.lost_blocks >= previous.lost_blocks
                ? sample.lost_blocks - previous.lost_blocks
                : sample.lost_blocks;
            const std::uint32_t delta_gap = sample.gap_blocks >= previous.gap_blocks
                ? sample.gap_blocks - previous.gap_blocks
                : sample.gap_blocks;
            // Use the actual elapsed time between samples rather than the configured
            // interval to stay accurate when submissions jitter, batch, or stall.
            const double observed_s = sample.steady_time_ns > previous.steady_time_ns
                ? std::clamp(
                    static_cast<double>(sample.steady_time_ns - previous.steady_time_ns) * 1e-9,
                    0.001, 5.0)
                : std::max(0.001, static_cast<double>(options_.interval_ms) / 1000.0);
            const double raw_lost = static_cast<double>(delta_lost) / observed_s;
            const double raw_gap = static_cast<double>(delta_gap) / observed_s;
            if (!ewma_rates_initialized_) {
                ewma_lost_rate_ = raw_lost;
                ewma_gap_rate_ = raw_gap;
                ewma_rates_initialized_ = true;
            } else {
                ewma_lost_rate_ += 0.25 * (raw_lost - ewma_lost_rate_);
                ewma_gap_rate_ += 0.25 * (raw_gap - ewma_gap_rate_);
            }
            const auto decision = evaluate(sample, ewma_lost_rate_, ewma_gap_rate_, ++sequence);
            decision_version_.fetch_add(1, std::memory_order_release);  // odd: writer active
            decision_time_ns_.store(decision.source_time_ns, std::memory_order_relaxed);
            decision_risk_score_.store(decision.risk_score, std::memory_order_relaxed);
            decision_mitigation_.store(static_cast<int>(decision.mitigation), std::memory_order_relaxed);
            decision_sequence_.store(decision.sequence, std::memory_order_relaxed);
            decision_version_.fetch_add(1, std::memory_order_release);  // even: publish complete
            previous = sample;
        }
        if (!saw_sample) {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_for(
                lock,
                std::chrono::milliseconds(options_.interval_ms),
                [this]() {
                    if (!running_.load(std::memory_order_relaxed)) {
                        return true;
                    }
                    return read_seq_.load(std::memory_order_acquire) != write_seq_.load(std::memory_order_acquire);
                }
            );
        }
    }
}

bool PredictorWorker::pop(PredictorTelemetrySample& sample) {
    const std::size_t read = read_seq_.load(std::memory_order_relaxed);
    const std::size_t write = write_seq_.load(std::memory_order_acquire);
    if (read == write) {
        return false;
    }
    sample = queue_[read % kQueueCapacity];
    read_seq_.store(read + 1, std::memory_order_release);
    return true;
}

PredictorDecision PredictorWorker::evaluate(
    const PredictorTelemetrySample& current,
    double smoothed_lost_rate,
    double smoothed_gap_rate,
    std::uint64_t sequence
) const {
    const double jitter_risk = clamp01(current.selected_jitter_ms / 5.0);
    const double latency_risk = clamp01((current.selected_latency_ms - 8.0) / 25.0);
    const double low_fill_risk = current.buffer_fill_ratio < 0.25f
        ? clamp01((0.25 - static_cast<double>(current.buffer_fill_ratio)) / 0.25)
        : 0.0;
    // smoothed_lost_rate and smoothed_gap_rate are in events/second.
    // 20 events/sec saturates risk (≈3 per 150 ms interval, matching prior calibration).
    const double event_risk = clamp01((smoothed_lost_rate + 2.0 * smoothed_gap_rate) / 20.0);
    const double drift_risk = clamp01(std::abs(current.drift_ppm) / 350.0);

    const double risk = (0.38 * jitter_risk)
        + (0.18 * latency_risk)
        + (0.20 * low_fill_risk)
        + (0.16 * event_risk)
        + (0.08 * drift_risk);

    PredictorMitigation mitigation = PredictorMitigation::None;
    if (risk >= options_.risk_high_threshold || event_risk >= 0.75) {
        mitigation = PredictorMitigation::RaiseSinkBuffer;
    } else if (risk >= options_.risk_medium_threshold && low_fill_risk >= 0.4) {
        mitigation = PredictorMitigation::RaiseSinkBufferLarge;
    } else if (risk >= options_.risk_medium_threshold
               && jitter_risk >= 0.70
               && low_fill_risk < 0.20
               && event_risk < 0.25
               && current.buffer_fill_ratio > 0.35f) {
        mitigation = PredictorMitigation::SwitchCodecOpusLowDelay;
    }

    return PredictorDecision {
        .sequence = sequence,
        .source_time_ns = current.steady_time_ns,
        .risk_score = risk,
        .mitigation = mitigation,
    };
}

}  // namespace peerjam
