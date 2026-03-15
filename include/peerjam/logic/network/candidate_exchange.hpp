#pragma once

#include "peerjam/logic/network/candidate.hpp"

#include <optional>
#include <string>
#include <vector>

namespace peerjam {

class CandidateExchangeClient {
  public:
    bool publish(
        const std::string& host,
        int port,
        const std::string& session,
        const std::string& user,
        const std::vector<NetworkCandidate>& candidates
    ) const;

    std::optional<std::vector<NetworkCandidate>> fetch(
        const std::string& host,
        int port,
        const std::string& session,
        const std::string& user,
        const std::string& peer_user
    ) const;
};

class CandidateExchangeServer {
  public:
    CandidateExchangeServer() = default;
    ~CandidateExchangeServer();

    bool start(int port);
    void stop();

  private:
    void run();
    void handle_client(int fd);

    int listen_fd_ {-1};
    bool running_ {false};
};

}  // namespace peerjam
