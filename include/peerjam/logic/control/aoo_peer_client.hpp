#pragma once

#include "aoo/aoo_net.h"
#include "peerjam/logic/audio/audio_runtime.hpp"
#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/network/candidate_gatherer.hpp"
#include "peerjam/logic/network/candidate_exchange.hpp"
#include "peerjam/logic/network/path_manager.hpp"
#include "peerjam/logic/probe/probe_runtime.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <atomic>
#include <optional>
#include <thread>

namespace peerjam {

class AooPeerClient {
  public:
    explicit AooPeerClient(PeerOptions options);
    ~AooPeerClient();

    int run();

  private:
    static int32_t send_callback(void* user, const char* data, int32_t size, void* addr);
    static int32_t event_handler(void* user, const aoo_event** events, int32_t count);
    static void tick_callback(void* user);
    void background_reprobe_loop();

    int start();
    void stop();
    void receiver_loop();
    void pump_client_events();
    void maybe_send_client_messages();
    bool wait_for_remote_peer();
    void publish_local_candidates();
    void try_fetch_remote_candidates(const std::string& peer_user);
    void handle_client_event(const aoo_event* event);
    static std::uint64_t session_token_for_group(const std::string& group);

    PeerOptions options_;
    UdpDatagramSocket socket_;
    CandidateGatherer candidate_gatherer_;
    CandidateExchangeClient candidate_exchange_;
    PathManager path_manager_;
    ProbeRuntime probe_;
    AudioRuntime audio_;
    aoonet_client* client_ {nullptr};
    std::thread client_thread_;
    std::thread receiver_thread_;
    std::thread reprobe_thread_;
    std::atomic<bool> receiver_running_ {false};
    std::atomic<bool> reprobe_running_ {false};
    std::atomic<bool> connected_ {false};
    std::optional<RemoteEndpoint> remote_endpoint_;
    std::uint64_t session_token_ {};
};

}  // namespace peerjam
