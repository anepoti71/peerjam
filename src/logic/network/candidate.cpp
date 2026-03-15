#include "peerjam/logic/network/candidate.hpp"

#include <arpa/inet.h>
#include <iomanip>
#include <sstream>

namespace peerjam {
namespace {

std::string quote(const std::string& value) {
    return "\"" + value + "\"";
}

std::string format_candidate(const NetworkCandidate& candidate) {
    std::ostringstream os;
    os << "{"
       << "\"type\":" << quote(candidate_type_name(candidate.type)) << ","
       << "\"foundation\":" << quote(candidate.foundation) << ","
       << "\"interface\":" << quote(candidate.interface_name) << ","
       << "\"address\":" << quote(candidate_address_string(candidate)) << ","
       << "\"priority\":" << candidate.priority << ","
       << "\"loopback\":" << (candidate.is_loopback ? "true" : "false")
       << "}";
    return os.str();
}

}  // namespace

const char* candidate_type_name(CandidateType type) {
    switch (type) {
        case CandidateType::Host:
            return "host";
        case CandidateType::ServerReflexive:
            return "srflx";
        case CandidateType::Relay:
            return "relay";
    }
    return "unknown";
}

std::string candidate_address_string(const NetworkCandidate& candidate) {
    char buffer[INET6_ADDRSTRLEN] {};
    if (candidate.addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&candidate.addr);
        ::inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer));
        return std::string(buffer) + ":" + std::to_string(ntohs(in->sin_port));
    }
    if (candidate.addr.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&candidate.addr);
        ::inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer));
        return "[" + std::string(buffer) + "]:" + std::to_string(ntohs(in6->sin6_port));
    }
    return "unknown";
}

std::string PathSelectionReport::to_json() const {
    std::ostringstream os;
    os << "{\n"
       << "  \"candidates_considered\": " << candidates_considered << ",\n"
       << "  \"active_path_selected\": " << (active_path_selected ? "true" : "false") << ",\n"
       << "  \"local_candidates\": [";
    for (std::size_t i = 0; i < local_candidates.size(); ++i) {
        if (i > 0) {
            os << ",";
        }
        os << format_candidate(local_candidates[i]);
    }
    os << "],\n"
       << "  \"remote_candidates\": [";
    for (std::size_t i = 0; i < remote_candidates.size(); ++i) {
        if (i > 0) {
            os << ",";
        }
        os << format_candidate(remote_candidates[i]);
    }
    os << "],\n"
       << "  \"pair_measurements\": [";
    for (std::size_t i = 0; i < pair_measurements.size(); ++i) {
        if (i > 0) {
            os << ",";
        }
        os << "{"
           << "\"local_foundation\":" << quote(pair_measurements[i].local.foundation) << ","
           << "\"remote_foundation\":" << quote(pair_measurements[i].remote.foundation) << ","
           << "\"local_address\":" << quote(candidate_address_string(pair_measurements[i].local)) << ","
           << "\"remote_address\":" << quote(candidate_address_string(pair_measurements[i].remote)) << ","
           << "\"score\":" << std::fixed << std::setprecision(3) << pair_measurements[i].score.score << ","
           << "\"latency_ms\":" << pair_measurements[i].score.latency_ms << ","
           << "\"jitter_ms\":" << pair_measurements[i].score.jitter_ms << ","
           << "\"loss_percent\":" << pair_measurements[i].score.loss_percent
           << "}";
    }
    os << "],\n"
       << "  \"selected_local\": " << format_candidate(selected_local) << ",\n"
       << "  \"selected_remote\": " << format_candidate(selected_remote) << ",\n"
       << "  \"reprobe_rounds\": " << reprobe_rounds << ",\n"
       << "  \"switch_count\": " << switch_count << ",\n"
       << "  \"switch_margin_score\": " << switch_margin_score << ",\n"
       << "  \"switch_consecutive_wins\": " << switch_consecutive_wins << ",\n"
       << "  \"switch_events\": [";
    for (std::size_t i = 0; i < switch_events.size(); ++i) {
        if (i > 0) {
            os << ",";
        }
        os << "{"
           << "\"from_local_foundation\":" << quote(switch_events[i].from_local_foundation) << ","
           << "\"to_local_foundation\":" << quote(switch_events[i].to_local_foundation) << ","
           << "\"previous_score\":" << switch_events[i].previous_score << ","
           << "\"next_score\":" << switch_events[i].next_score
           << "}";
    }
    os << "],\n"
       << "  \"selected_score\": {"
       << "\"latency_ms\":" << std::fixed << std::setprecision(3) << selected_score.latency_ms << ","
       << "\"jitter_ms\":" << selected_score.jitter_ms << ","
       << "\"loss_percent\":" << selected_score.loss_percent << ","
       << "\"score\":" << selected_score.score
       << "}\n"
       << "}";
    return os.str();
}

}  // namespace peerjam
