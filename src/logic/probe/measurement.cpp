#include "peerjam/logic/probe/measurement.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>

namespace peerjam {
namespace {

double percentile(const std::vector<double>& values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    auto ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const auto rank = static_cast<std::size_t>(std::ceil((p / 100.0) * ordered.size()));
    return ordered[std::min<std::size_t>(ordered.size() - 1, std::max<std::size_t>(rank, 1) - 1)];
}

std::string quote(const std::string& value) {
    return "\"" + value + "\"";
}

}  // namespace

MeasurementTracker::MeasurementTracker(std::uint32_t expected_packets)
    : expected_packets_(expected_packets) {}

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
        rec.sample_rate_hz = 48000;
        rec.frame_samples = 128;
        rec.initial_buffer_ms = 10.0;
        rec.rationale = "No direct peer path was established through AOO transport.";
        return rec;
    }

    if (report.loss_percent > 1.0 || one_way_p95 > 30.0 || jitter_p95 > 5.0) {
        rec.status = "not_ready";
        rec.codec = "opus-lowdelay";
        rec.sample_rate_hz = 48000;
        rec.frame_samples = 128;
        rec.initial_buffer_ms = std::clamp(jitter_p95 * 2.5, 8.0, 20.0);
        rec.rationale = "Current path is too unstable for sub-30 ms rhythm-sensitive music.";
        return rec;
    }

    if (report.loss_percent <= 0.1 && one_way_p95 <= 12.0 && jitter_p95 <= 1.0) {
        rec.status = "ready";
        rec.codec = "pcm";
        rec.sample_rate_hz = 48000;
        rec.frame_samples = 64;
        rec.initial_buffer_ms = 5.0;
        rec.rationale = "Path supports aggressive PCM-first settings similar to JackTrip-style low-overhead streaming.";
        return rec;
    }

    if (report.loss_percent <= 0.5 && one_way_p95 <= 18.0 && jitter_p95 <= 2.5) {
        rec.status = "ready";
        rec.codec = "pcm-or-opus-lowdelay";
        rec.sample_rate_hz = 48000;
        rec.frame_samples = 128;
        rec.initial_buffer_ms = std::clamp(jitter_p95 * 2.0, 5.0, 8.0);
        rec.rationale = "Path is usable for music; start conservatively and prefer PCM on wired clean links.";
        return rec;
    }

    rec.status = "marginal";
    rec.codec = "opus-lowdelay";
    rec.sample_rate_hz = 48000;
    rec.frame_samples = 128;
    rec.initial_buffer_ms = std::clamp(jitter_p95 * 2.2, 7.5, 12.0);
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
       << "  \"recommendation\": {\n"
       << "    \"status\": " << quote(recommendation.status) << ",\n"
       << "    \"codec\": " << quote(recommendation.codec) << ",\n"
       << "    \"sample_rate_hz\": " << recommendation.sample_rate_hz << ",\n"
       << "    \"frame_samples\": " << recommendation.frame_samples << ",\n"
       << "    \"initial_buffer_ms\": " << recommendation.initial_buffer_ms << ",\n"
       << "    \"rationale\": " << quote(recommendation.rationale) << "\n"
       << "  }\n"
       << "}";
    return os.str();
}

}  // namespace peerjam
