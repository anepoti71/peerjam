#pragma once

#include "aoo/aoo_net.h"
#include "peerjam/logic/audio/audio_runtime.hpp"
#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/network/candidate_gatherer.hpp"
#include "peerjam/logic/network/candidate_exchange.hpp"
#include "peerjam/logic/network/path_manager.hpp"
#include "peerjam/logic/probe/probe_runtime.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"
#include "peerjam/logic/ui/realtime_ui_server.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace peerjam {

class AooPeerClient {
  public:
    explicit AooPeerClient(PeerOptions options);
    ~AooPeerClient();

    int run();
    int run_network_test();

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
    std::shared_ptr<RemoteEndpoint> load_remote_endpoint() const;
    bool apply_selected_path(const PathSelectionReport& report);
    void update_selected_path_metrics(const PathSelectionReport& report);
    void maybe_publish_ui_telemetry();
    static double estimated_bandwidth_kbps(const std::string& codec);
    void elect_timing_authority(const std::string& remote_user);
    std::string timing_state_json() const;
    void handle_client_event(const aoo_event* event);
    static std::uint64_t session_token_for_group(const std::string& group);
    int run_qualification(bool include_audio);
    void print_qualification_report(const PathSelectionReport& path_report, const MeasurementReport& probe_report) const;
    void apply_input_session_profile();

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
    std::shared_ptr<RemoteEndpoint> remote_endpoint_;
    std::optional<NetworkCandidate> applied_local_candidate_;
    bool remote_candidates_fetched_ {false};
    std::chrono::steady_clock::time_point next_candidate_fetch_at_ {};
    std::uint64_t session_token_ {};
    std::unique_ptr<RealtimeUiServer> ui_server_;
    std::atomic<double> selected_latency_ms_ {0.0};
    std::atomic<double> selected_jitter_ms_ {0.0};
    std::chrono::steady_clock::time_point next_ui_publish_at_ {};
    mutable std::mutex timing_mutex_;
    std::string timing_role_ {"unknown"};
    std::string timing_master_user_ {"unknown"};
};

}  // namespace peerjam
