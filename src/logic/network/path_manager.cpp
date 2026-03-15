#include "peerjam/logic/network/path_manager.hpp"

namespace peerjam {

PathManager::PathManager(const PeerOptions& options)
    : switch_margin_score_(options.path_switch_margin_score)
    , switch_consecutive_wins_(options.path_switch_consecutive_wins)
    , probe_engine_(options) {}

void PathManager::set_local_candidates(std::vector<NetworkCandidate> candidates) {
    std::lock_guard lock(mutex_);
    local_candidates_ = std::move(candidates);
}

void PathManager::set_remote_candidates(std::vector<NetworkCandidate> candidates) {
    std::lock_guard lock(mutex_);
    remote_candidates_ = std::move(candidates);
    if (!remote_candidates_.empty()) {
        active_remote_candidate_ = remote_candidates_.front();
    }
}

PathSelectionReport PathManager::measure_and_select_active_path(std::uint64_t session_token) {
    std::vector<NetworkCandidate> local_candidates;
    std::vector<NetworkCandidate> remote_candidates;
    {
        std::lock_guard lock(mutex_);
        local_candidates = local_candidates_;
        remote_candidates = remote_candidates_;
    }
    const auto pair_measurements = probe_engine_.measure_candidate_pairs(local_candidates, remote_candidates, session_token);
    std::lock_guard lock(mutex_);
    reprobe_rounds_ = 1;
    return select_best_report(pair_measurements, false);
}

PathSelectionReport PathManager::reprobe_and_maybe_switch(std::uint64_t session_token) {
    std::vector<NetworkCandidate> local_candidates;
    std::vector<NetworkCandidate> remote_candidates;
    {
        std::lock_guard lock(mutex_);
        local_candidates = local_candidates_;
        remote_candidates = remote_candidates_;
    }
    const auto pair_measurements = probe_engine_.measure_candidate_pairs(local_candidates, remote_candidates, session_token ^ 0xD1B54A32D192ED03ULL);
    std::lock_guard lock(mutex_);
    ++reprobe_rounds_;
    return select_best_report(pair_measurements, true);
}

PathSelectionReport PathManager::current_report() const {
    std::lock_guard lock(mutex_);
    PathSelectionReport report;
    report.local_candidates = local_candidates_;
    report.remote_candidates = remote_candidates_;
    report.pair_measurements = last_pair_measurements_;
    report.candidates_considered = static_cast<int>(local_candidates_.size() * remote_candidates_.size());
    report.active_path_selected = active_local_candidate_.has_value() && active_remote_candidate_.has_value() && active_score_.has_value();
    report.reprobe_rounds = reprobe_rounds_;
    report.switch_count = switch_count_;
    report.switch_margin_score = switch_margin_score_;
    report.switch_consecutive_wins = switch_consecutive_wins_;
    report.switch_events = switch_events_;
    if (active_local_candidate_) {
        report.selected_local = *active_local_candidate_;
    }
    if (active_remote_candidate_) {
        report.selected_remote = *active_remote_candidate_;
    }
    if (active_score_) {
        report.selected_score = *active_score_;
    }
    return report;
}

PathSelectionReport PathManager::select_best_report(
    std::vector<CandidatePairMeasurement> pair_measurements,
    bool allow_switch
) {
    PathSelectionReport selection;
    selection.local_candidates = local_candidates_;
    selection.remote_candidates = remote_candidates_;
    selection.pair_measurements = std::move(pair_measurements);
    selection.candidates_considered = static_cast<int>(local_candidates_.size() * remote_candidates_.size());
    selection.reprobe_rounds = reprobe_rounds_;
    selection.switch_count = switch_count_;
    selection.switch_margin_score = switch_margin_score_;
    selection.switch_consecutive_wins = switch_consecutive_wins_;
    selection.switch_events = switch_events_;
    if (selection.pair_measurements.empty()) {
        return selection;
    }
    last_pair_measurements_ = selection.pair_measurements;

    const CandidatePairMeasurement* best = &selection.pair_measurements.front();
    for (const auto& pair : selection.pair_measurements) {
        if (pair.score.score < best->score.score) {
            best = &pair;
        }
    }

    if (!active_local_candidate_ || !active_score_ || !allow_switch) {
        active_local_candidate_ = best->local;
        active_remote_candidate_ = best->remote;
        active_score_ = best->score;
        pending_win_count_ = 0;
        pending_local_candidate_.reset();
        pending_remote_candidate_.reset();
    } else {
        const bool improvement = best->score.score + switch_margin_score_ < active_score_->score
            && (
                best->local.foundation != active_local_candidate_->foundation
                || best->remote.foundation != active_remote_candidate_->foundation
            );
        if (improvement) {
            if (pending_local_candidate_ && pending_remote_candidate_
                && pending_local_candidate_->foundation == best->local.foundation
                && pending_remote_candidate_->foundation == best->remote.foundation) {
                ++pending_win_count_;
            } else {
                pending_local_candidate_ = best->local;
                pending_remote_candidate_ = best->remote;
                pending_win_count_ = 1;
            }
            if (pending_win_count_ >= switch_consecutive_wins_) {
                switch_events_.push_back(PathSwitchEvent {
                    .from_local_foundation = active_local_candidate_->foundation,
                    .to_local_foundation = best->local.foundation,
                    .from_remote_foundation = active_remote_candidate_->foundation,
                    .to_remote_foundation = best->remote.foundation,
                    .previous_score = active_score_->score,
                    .next_score = best->score.score,
                });
                ++switch_count_;
                active_local_candidate_ = best->local;
                active_remote_candidate_ = best->remote;
                active_score_ = best->score;
                pending_win_count_ = 0;
                pending_local_candidate_.reset();
                pending_remote_candidate_.reset();
            }
        } else {
            pending_win_count_ = 0;
            pending_local_candidate_.reset();
            pending_remote_candidate_.reset();
        }
    }

    selection.active_path_selected = active_local_candidate_.has_value() && active_remote_candidate_.has_value() && active_score_.has_value();
    selection.selected_local = *active_local_candidate_;
    selection.selected_remote = *active_remote_candidate_;
    selection.selected_score = *active_score_;
    selection.switch_count = switch_count_;
    selection.switch_events = switch_events_;
    return selection;
}

}  // namespace peerjam
