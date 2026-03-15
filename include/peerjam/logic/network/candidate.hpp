#pragma once

#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <vector>

#include "peerjam/logic/probe/measurement.hpp"

namespace peerjam {

enum class CandidateType : std::uint8_t {
    Host,
    ServerReflexive,
    Relay
};

struct NetworkCandidate {
    CandidateType type {CandidateType::Host};
    std::string foundation;
    std::string interface_name;
    sockaddr_storage addr {};
    socklen_t addr_len {};
    bool is_ipv6 {false};
    bool is_loopback {false};
    int priority {};
};

struct PathScore {
    double latency_ms {};
    double jitter_ms {};
    double loss_percent {};
    double score {};
};

struct CandidatePairMeasurement {
    NetworkCandidate local;
    NetworkCandidate remote;
    MeasurementReport probe;
    PathScore score;
};

struct PathSwitchEvent {
    std::string from_local_foundation;
    std::string to_local_foundation;
    double previous_score {};
    double next_score {};
};

struct PathSelectionReport {
    std::vector<NetworkCandidate> local_candidates;
    std::vector<NetworkCandidate> remote_candidates;
    std::vector<CandidatePairMeasurement> pair_measurements;
    int candidates_considered {};
    bool active_path_selected {};
    int reprobe_rounds {};
    int switch_count {};
    double switch_margin_score {};
    int switch_consecutive_wins {};
    NetworkCandidate selected_local;
    NetworkCandidate selected_remote;
    PathScore selected_score;
    std::vector<PathSwitchEvent> switch_events;

    std::string to_json() const;
};

const char* candidate_type_name(CandidateType type);
std::string candidate_address_string(const NetworkCandidate& candidate);
bool parse_candidate_address(int family, const std::string& ip, int port, sockaddr_storage& addr, socklen_t& addr_len);

}  // namespace peerjam
