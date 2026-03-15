#include "peerjam/logic/network/path_scorer.hpp"

#include <algorithm>

namespace peerjam {

PathScore PathScorer::score(const MeasurementReport& report, const NetworkCandidate& local, const NetworkCandidate& remote) const {
    PathScore result;
    result.latency_ms = report.one_way_ms.p95_ms > 0.0 ? report.one_way_ms.p95_ms : report.one_way_ms.average_ms;
    result.jitter_ms = report.ingress_jitter_ms.p95_ms > 0.0 ? report.ingress_jitter_ms.p95_ms : report.ingress_jitter_ms.average_ms;
    result.loss_percent = report.loss_percent;

    const double loss_penalty = report.loss_percent <= 0.0 ? 0.0 : (report.loss_percent * 8.0);
    const double loopback_penalty = (local.is_loopback || remote.is_loopback) ? 0.25 : 0.0;
    const double relay_penalty = (local.type == CandidateType::Relay || remote.type == CandidateType::Relay) ? 25.0 : 0.0;
    result.score = result.latency_ms + (2.0 * result.jitter_ms) + loss_penalty + loopback_penalty + relay_penalty;
    return result;
}

}  // namespace peerjam
