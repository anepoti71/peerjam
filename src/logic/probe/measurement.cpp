#include "peerjam/common/json_utils.hpp"
#include "peerjam/common/stats_utils.hpp"
#include "peerjam/logic/probe/measurement.hpp"

#include <iomanip>
#include <numeric>

namespace peerjam {
namespace {

}  // namespace

MeasurementTracker::MeasurementTracker(std::uint32_t expected_packets, RecommendationPolicyOptions policy)
    : expected_packets_(expected_packets)
    , policy_(policy) {}

void MeasurementTracker::mark_sent(std::uint32_t sequence, std::uint64_t sent_at_ns) {
    sent_at_ns_[sequence] = sent_at_ns;
}

void MeasurementTracker::on_probe_received(std::uint32_t sequence, std::uint64_t arrival_ns) {
    direct_path_established_ = true;
    ++ingress_packets_;
    if (last_probe_sequence_) {
        if (sequence < *last_probe_sequence_) {
            ++reordered_packets_;
        } else if (sequence > *last_probe_sequence_ + 1) {
            missing_packets_ += sequence - *last_probe_sequence_ - 1;
        }
    }
    if (last_probe_arrival_ns_) {
        const double interval_ms = static_cast<double>(arrival_ns - *last_probe_arrival_ns_) / 1'000'000.0;
        ingress_interval_ms_.push_back(interval_ms);
        if (last_ingress_interval_ms_) {
            ingress_jitter_ms_.push_back(std::abs(interval_ms - *last_ingress_interval_ms_));
        }
        last_ingress_interval_ms_ = interval_ms;
    }
    last_probe_arrival_ns_ = arrival_ns;
    last_probe_sequence_ = sequence;
}

void MeasurementTracker::on_echo_received(std::uint32_t sequence, std::uint64_t arrival_ns) {
    direct_path_established_ = true;
    const auto it = sent_at_ns_.find(sequence);
    if (it == sent_at_ns_.end()) {
        return;
    }
    ++echoed_packets_;
    if (last_echo_sequence_ && sequence < *last_echo_sequence_) {
        ++echo_reordered_packets_;
    }
    last_echo_sequence_ = sequence;
    const double rtt_ms = static_cast<double>(arrival_ns - it->second) / 1'000'000.0;
    rtt_ms_.push_back(rtt_ms);
    if (last_rtt_ms_) {
        echo_jitter_ms_.push_back(std::abs(rtt_ms - *last_rtt_ms_));
    }
    last_rtt_ms_ = rtt_ms;
}

void MeasurementTracker::set_direct_path_established(bool value) {
    direct_path_established_ = value;
}

SummaryStats MeasurementTracker::summarize(const std::vector<double>& values_ms) {
    if (values_ms.empty()) {
        return {};
    }
    const double sum = std::accumulate(values_ms.begin(), values_ms.end(), 0.0);
    return SummaryStats {
        .count = values_ms.size(),
        .minimum_ms = *std::min_element(values_ms.begin(), values_ms.end()),
        .average_ms = sum / static_cast<double>(values_ms.size()),
        .p95_ms = percentile(values_ms, 95.0),
        .maximum_ms = *std::max_element(values_ms.begin(), values_ms.end()),
    };
}

Recommendation MeasurementTracker::recommend(const MeasurementReport& report) const {
    Recommendation rec;
    const double one_way_p95 = report.one_way_ms.p95_ms;
    const double jitter_p95 = report.ingress_jitter_ms.p95_ms;

    if (!report.direct_path_established) {
        rec.status = "not_ready";
        rec.codec = "none";
        rec.sample_rate_hz = policy_.sample_rate_hz;
        rec.frame_samples = policy_.fallback_frame_samples;
        rec.initial_buffer_ms = 10.0;
        rec.rationale = "No direct peer path was established through AOO transport.";
        return rec;
    }

    if (report.loss_percent > policy_.not_ready_loss_percent
        || one_way_p95 > policy_.not_ready_one_way_p95_ms
        || jitter_p95 > policy_.not_ready_jitter_p95_ms) {
        rec.status = "not_ready";
        rec.codec = "opus-lowdelay";
        rec.sample_rate_hz = policy_.sample_rate_hz;
        rec.frame_samples = policy_.fallback_frame_samples;
        rec.initial_buffer_ms = std::clamp(
            jitter_p95 * 2.5,
            policy_.not_ready_buffer_min_ms,
            policy_.not_ready_buffer_max_ms
        );
        rec.rationale = "Current path is too unstable for sub-30 ms rhythm-sensitive music.";
        return rec;
    }

    if (report.loss_percent <= policy_.aggressive_ready_loss_percent
        && one_way_p95 <= policy_.aggressive_ready_one_way_p95_ms
        && jitter_p95 <= policy_.aggressive_ready_jitter_p95_ms) {
        rec.status = "ready";
        rec.codec = "pcm";
        rec.sample_rate_hz = policy_.sample_rate_hz;
        rec.frame_samples = policy_.aggressive_frame_samples;
        rec.initial_buffer_ms = policy_.ready_buffer_min_ms;
        rec.rationale = "Path supports aggressive PCM-first settings similar to JackTrip-style low-overhead streaming.";
        return rec;
    }

    if (report.loss_percent <= policy_.conservative_ready_loss_percent
        && one_way_p95 <= policy_.conservative_ready_one_way_p95_ms
        && jitter_p95 <= policy_.conservative_ready_jitter_p95_ms) {
        rec.status = "ready";
        rec.codec = "pcm-or-opus-lowdelay";
        rec.sample_rate_hz = policy_.sample_rate_hz;
        rec.frame_samples = policy_.conservative_frame_samples;
        rec.initial_buffer_ms = std::clamp(
            jitter_p95 * 2.0,
            policy_.ready_buffer_min_ms,
            policy_.ready_buffer_max_ms
        );
        rec.rationale = "Path is usable for music; start conservatively and prefer PCM on wired clean links.";
        return rec;
    }

    rec.status = "marginal";
    rec.codec = "opus-lowdelay";
    rec.sample_rate_hz = policy_.sample_rate_hz;
    rec.frame_samples = policy_.fallback_frame_samples;
    rec.initial_buffer_ms = std::clamp(
        jitter_p95 * 2.2,
        policy_.marginal_buffer_min_ms,
        policy_.marginal_buffer_max_ms
    );
    rec.rationale = "Path may work for some musical interaction, but timing margin is limited.";
    return rec;
}

MeasurementReport MeasurementTracker::finish() const {
    MeasurementReport report;
    report.direct_path_established = direct_path_established_;
    report.rtt_ms = summarize(rtt_ms_);
    std::vector<double> one_way_ms;
    one_way_ms.reserve(rtt_ms_.size());
    for (double value : rtt_ms_) {
        one_way_ms.push_back(value / 2.0);
    }
    report.one_way_ms = summarize(one_way_ms);
    report.ingress_interval_ms = summarize(ingress_interval_ms_);
    report.ingress_jitter_ms = summarize(ingress_jitter_ms_);
    report.echo_jitter_ms = summarize(echo_jitter_ms_);
    report.sent_packets = expected_packets_;
    report.echoed_packets = echoed_packets_;
    report.ingress_packets = ingress_packets_;
    report.reordered_packets = reordered_packets_;
    report.echo_reordered_packets = echo_reordered_packets_;
    report.missing_packets = missing_packets_;
    const auto lost = expected_packets_ > echoed_packets_ ? expected_packets_ - echoed_packets_ : 0U;
    report.loss_percent = expected_packets_ > 0 ? (static_cast<double>(lost) * 100.0 / static_cast<double>(expected_packets_)) : 0.0;
    report.recommendation = recommend(report);
    return report;
}

std::string MeasurementReport::to_json() const {
    auto fmt_stats = [](const SummaryStats& stats) {
        std::ostringstream os;
        os << "{"
           << "\"count\":" << stats.count << ","
           << "\"minimum_ms\":" << std::fixed << std::setprecision(3) << stats.minimum_ms << ","
           << "\"average_ms\":" << stats.average_ms << ","
           << "\"p95_ms\":" << stats.p95_ms << ","
           << "\"maximum_ms\":" << stats.maximum_ms
           << "}";
        return os.str();
    };

    std::ostringstream os;
    os << "{\n"
       << "  \"direct_path_established\": " << (direct_path_established ? "true" : "false") << ",\n"
       << "  \"sent_packets\": " << sent_packets << ",\n"
       << "  \"echoed_packets\": " << echoed_packets << ",\n"
       << "  \"ingress_packets\": " << ingress_packets << ",\n"
       << "  \"loss_percent\": " << std::fixed << std::setprecision(3) << loss_percent << ",\n"
       << "  \"reordered_packets\": " << reordered_packets << ",\n"
       << "  \"echo_reordered_packets\": " << echo_reordered_packets << ",\n"
       << "  \"missing_packets\": " << missing_packets << ",\n"
       << "  \"rtt_ms\": " << fmt_stats(rtt_ms) << ",\n"
       << "  \"estimated_one_way_ms\": " << fmt_stats(one_way_ms) << ",\n"
       << "  \"ingress_interval_ms\": " << fmt_stats(ingress_interval_ms) << ",\n"
       << "  \"ingress_jitter_ms\": " << fmt_stats(ingress_jitter_ms) << ",\n"
       << "  \"echo_jitter_ms\": " << fmt_stats(echo_jitter_ms) << ",\n"
       << "  \"recommendation\": " << recommendation.to_json() << "\n"
       << "}";
    return os.str();
}

std::string Recommendation::to_json() const {
    std::ostringstream os;
    os << "{\n"
       << "    \"status\": " << json_quote(status) << ",\n"
       << "    \"codec\": " << json_quote(codec) << ",\n"
       << "    \"sample_rate_hz\": " << sample_rate_hz << ",\n"
       << "    \"frame_samples\": " << frame_samples << ",\n"
       << "    \"initial_buffer_ms\": " << initial_buffer_ms << ",\n"
       << "    \"rationale\": " << json_quote(rationale) << "\n"
       << "  }";
    return os.str();
}

}  // namespace peerjam
