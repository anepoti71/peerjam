#pragma once

#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/network/candidate.hpp"
#include "peerjam/logic/network/path_scorer.hpp"

#include <cstdint>
#include <vector>

namespace peerjam {

class CandidateProbeEngine {
  public:
    explicit CandidateProbeEngine(const PeerOptions& options);

    std::vector<CandidatePairMeasurement> measure_candidate_pairs(
        const std::vector<NetworkCandidate>& local_candidates,
        const std::vector<NetworkCandidate>& remote_candidates,
        std::uint64_t session_token
    ) const;

  private:
    PeerOptions options_;
    PathScorer scorer_;
};

}  // namespace peerjam
