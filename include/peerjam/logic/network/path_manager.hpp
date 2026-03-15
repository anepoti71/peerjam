#pragma once

#include "peerjam/logic/network/candidate.hpp"
#include "peerjam/logic/network/candidate_gatherer.hpp"
#include "peerjam/logic/network/path_probe_engine.hpp"
#include "peerjam/logic/network/path_scorer.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace peerjam {

class PathManager {
  public:
    explicit PathManager(const PeerOptions& options);

    void set_local_candidates(std::vector<NetworkCandidate> candidates);
    void set_remote_candidates(std::vector<NetworkCandidate> candidates);
    [[nodiscard]] const std::optional<NetworkCandidate>& active_remote_candidate() const { return active_remote_candidate_; }
    [[nodiscard]] const std::vector<NetworkCandidate>& local_candidates() const { return local_candidates_; }
    [[nodiscard]] const std::vector<NetworkCandidate>& remote_candidates() const { return remote_candidates_; }
    PathSelectionReport measure_and_select_active_path(std::uint64_t session_token);
    PathSelectionReport reprobe_and_maybe_switch(std::uint64_t session_token);
    PathSelectionReport current_report() const;

  private:
    PathSelectionReport select_best_report(
        std::vector<CandidatePairMeasurement> pair_measurements,
        bool allow_switch
    );

    std::vector<NetworkCandidate> local_candidates_;
    std::vector<NetworkCandidate> remote_candidates_;
    std::optional<NetworkCandidate> active_remote_candidate_;
    std::optional<NetworkCandidate> active_local_candidate_;
    std::optional<PathScore> active_score_;
    std::vector<CandidatePairMeasurement> last_pair_measurements_;
    std::vector<PathSwitchEvent> switch_events_;
    int reprobe_rounds_ {};
    int switch_count_ {};
    int pending_win_count_ {};
    std::optional<NetworkCandidate> pending_local_candidate_;
    std::optional<PathScore> pending_score_;
    double switch_margin_score_ {};
    int switch_consecutive_wins_ {};
    CandidateProbeEngine probe_engine_;
    mutable std::mutex mutex_;
};

}  // namespace peerjam
