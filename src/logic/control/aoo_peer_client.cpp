#include "peerjam/app/session_profile.hpp"
#include "peerjam/common/json_utils.hpp"
#include "peerjam/common/logger.hpp"
#include "peerjam/logic/control/aoo_peer_client.hpp"

#include "aoo/aoo.h"
#include "aoo/aoo_types.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <thread>

namespace peerjam {
namespace {

constexpr int kPollTimeoutMs = 20;
constexpr auto kCandidateFetchRetryInterval = std::chrono::milliseconds(500);

std::uint64_t stable_fnv1a_64(const std::string& text) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}

RemoteEndpoint endpoint_from_candidate(const NetworkCandidate& candidate, const std::string& user) {
    RemoteEndpoint endpoint;
    endpoint.addr = candidate.addr;
    endpoint.addr_len = candidate.addr_len;
    endpoint.user = user;
    return endpoint;
}

}  // namespace

AooPeerClient::AooPeerClient(PeerOptions options)
    : options_(std::move(options))
    , path_manager_(options_)
    , probe_(options_)
    , audio_(options_.audio) {
    apply_input_session_profile();
}

AooPeerClient::~AooPeerClient() {
    stop();
}

int32_t AooPeerClient::send_callback(void* user, const char* data, int32_t size, void* addr) {
    auto* self = static_cast<AooPeerClient*>(user);
    const auto* sa = static_cast<const sockaddr*>(addr);
    if (!self || !self->socket_.is_open() || !sa || !data || size <= 0) {
        return -1;
    }
    const socklen_t len = sa->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_storage);
    sockaddr_storage remote {};
    std::memcpy(&remote, sa, static_cast<std::size_t>(len));
    return self->socket_.send_to(remote, len, data, static_cast<std::size_t>(size));
}

int32_t AooPeerClient::event_handler(void* user, const aoo_event** events, int32_t count) {
    auto* self = static_cast<AooPeerClient*>(user);
    if (!self) {
        return 0;
    }
    for (int32_t i = 0; i < count; ++i) {
        self->handle_client_event(events[i]);
    }
    return count;
}

void AooPeerClient::tick_callback(void* user) {
    auto* self = static_cast<AooPeerClient*>(user);
    if (!self) {
        return;
    }
    self->pump_client_events();
    self->maybe_send_client_messages();
}

void AooPeerClient::background_reprobe_loop() {
    while (reprobe_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options_.path_reprobe_interval_ms));
        if (!reprobe_running_.load()) {
            break;
        }
        if (load_remote_endpoint()) {
            const auto report = path_manager_.reprobe_and_maybe_switch(session_token_);
            apply_selected_path(report);
        }
    }
}

int AooPeerClient::start() {
    aoo_initialize();
    if (socket_.open(options_.udp_port) != 0) {
        PJ_LOG(Error, "failed to open udp port " << options_.udp_port);
        return 1;
    }
    auto local_candidates = candidate_gatherer_.gather_local_candidates(options_.udp_port);
    if (const auto srflx = candidate_gatherer_.gather_server_reflexive_candidate(
            socket_,
            options_.stun_host,
            options_.stun_port,
            options_.udp_port
        )) {
        local_candidates.push_back(*srflx);
    }
    path_manager_.set_local_candidates(std::move(local_candidates));
    publish_local_candidates();

    client_ = aoonet_client_new(this, &AooPeerClient::send_callback, options_.udp_port);
    if (!client_) {
        PJ_LOG(Error, "failed to create AOO client");
        return 1;
    }

    receiver_running_.store(true);
    reprobe_running_.store(false);
    remote_candidates_fetched_ = false;
    next_candidate_fetch_at_ = std::chrono::steady_clock::now();
    client_thread_ = std::thread([this]() { aoonet_client_run(client_); });
    receiver_thread_ = std::thread([this]() { receiver_loop(); });
    return 0;
}

void AooPeerClient::stop() {
    audio_.teardown();
    reprobe_running_.store(false);
    receiver_running_.store(false);
    if (client_) {
        aoonet_client_disconnect(client_);
        aoonet_client_quit(client_);
    }
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (reprobe_thread_.joinable()) {
        reprobe_thread_.join();
    }
    if (client_thread_.joinable()) {
        client_thread_.join();
    }
    if (client_) {
        aoonet_client_free(client_);
        client_ = nullptr;
    }
}

void AooPeerClient::receiver_loop() {
    std::array<std::uint8_t, AOO_MAXPACKETSIZE> buffer {};
    while (receiver_running_.load()) {
        pollfd pfd {};
        pfd.fd = socket_.native_handle();
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, kPollTimeoutMs) <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        sockaddr_storage addr {};
        socklen_t addr_len = sizeof(addr);
        const int n = socket_.receive(buffer.data(), buffer.size(), addr, addr_len);
        if (n <= 0) {
            continue;
        }

        if (probe_.handle_datagram(buffer.data(), static_cast<std::size_t>(n), addr, addr_len, socket_)) {
            continue;
        }

        int32_t net_type = 0;
        if (aoonet_parse_pattern(reinterpret_cast<const char*>(buffer.data()), n, &net_type) > 0) {
            aoonet_client_handle_message(client_, reinterpret_cast<const char*>(buffer.data()), n, &addr);
            continue;
        }

        if (audio_.handle_datagram(buffer.data(), static_cast<std::size_t>(n))) {
            continue;
        }
    }
}

void AooPeerClient::pump_client_events() {
    if (client_) {
        aoonet_client_handle_events(client_, &AooPeerClient::event_handler, this);
    }
}

void AooPeerClient::maybe_send_client_messages() {
    if (client_) {
        aoonet_client_send(client_);
    }
}

bool AooPeerClient::wait_for_remote_peer() {
    if (aoonet_client_connect(client_, options_.server_host.c_str(), options_.server_port, options_.user.c_str(), options_.user_password.c_str()) <= 0) {
        PJ_LOG(Error, "failed to connect to signaling server");
        return false;
    }

    bool joined_group = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        pump_client_events();
        maybe_send_client_messages();
        if (connected_.load() && !joined_group) {
            if (aoonet_client_group_join(client_, options_.group.c_str(), options_.group_password.c_str()) > 0) {
                joined_group = true;
            }
        }
        const auto remote_endpoint = load_remote_endpoint();
        if (remote_endpoint && !remote_candidates_fetched_ && std::chrono::steady_clock::now() >= next_candidate_fetch_at_) {
            try_fetch_remote_candidates(remote_endpoint->user);
        }
        if (load_remote_endpoint()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

void AooPeerClient::publish_local_candidates() {
    candidate_exchange_.publish(
        options_.server_host,
        options_.candidate_exchange_port,
        options_.group,
        options_.user,
        path_manager_.local_candidates()
    );
}

void AooPeerClient::try_fetch_remote_candidates(const std::string& peer_user) {
    if (peer_user.empty()) {
        return;
    }
    const auto remote_candidates = candidate_exchange_.fetch(
        options_.server_host,
        options_.candidate_exchange_port,
        options_.group,
        options_.user,
        peer_user
    );
    if (remote_candidates && !remote_candidates->empty()) {
        path_manager_.set_remote_candidates(*remote_candidates);
        remote_candidates_fetched_ = true;
    } else {
        next_candidate_fetch_at_ = std::chrono::steady_clock::now() + kCandidateFetchRetryInterval;
    }
}

std::shared_ptr<RemoteEndpoint> AooPeerClient::load_remote_endpoint() const {
    return std::atomic_load(&remote_endpoint_);
}

bool AooPeerClient::apply_selected_path(const PathSelectionReport& report) {
    if (!report.active_path_selected) {
        return false;
    }

    if (!applied_local_candidate_
        || applied_local_candidate_->foundation != report.selected_local.foundation
        || candidate_address_string(*applied_local_candidate_) != candidate_address_string(report.selected_local)) {
        if (socket_.rebind(report.selected_local.addr, report.selected_local.addr_len) != 0) {
            return false;
        }
        applied_local_candidate_ = report.selected_local;
    }

    const auto current = load_remote_endpoint();
    const std::string remote_user = current ? current->user : "unknown";
    std::atomic_store(&remote_endpoint_, std::make_shared<RemoteEndpoint>(endpoint_from_candidate(report.selected_remote, remote_user)));
    return true;
}

void AooPeerClient::elect_timing_authority(const std::string& remote_user) {
    std::lock_guard lock(timing_mutex_);
    // Two-party session assumption: deterministic lexical ordering between local and one remote user.
    if (remote_user.empty()) {
        timing_role_ = "unknown";
        timing_master_user_ = "unknown";
        return;
    }
    if (options_.user <= remote_user) {
        timing_role_ = "master";
        timing_master_user_ = options_.user;
    } else {
        timing_role_ = "follower";
        timing_master_user_ = remote_user;
    }
}

std::string AooPeerClient::timing_state_json() const {
    std::lock_guard lock(timing_mutex_);
    return "{"
        "\"role\":" + json_quote(timing_role_) + ","
        "\"master_user\":" + json_quote(timing_master_user_)
        + "}";
}

void AooPeerClient::handle_client_event(const aoo_event* event) {
    switch (event->type) {
        case AOONET_CLIENT_CONNECT_EVENT:
            connected_.store(true);
            PJ_LOG(Info, "connected_to_signal_server");
            break;
        case AOONET_CLIENT_DISCONNECT_EVENT:
            connected_.store(false);
            {
                std::lock_guard lock(timing_mutex_);
                timing_role_ = "unknown";
                timing_master_user_ = "unknown";
            }
            PJ_LOG(Info, "disconnected_from_signal_server");
            break;
        case AOONET_CLIENT_GROUP_JOIN_EVENT:
            PJ_LOG(Info, "joined_group=" << options_.group);
            break;
        case AOONET_CLIENT_PEER_JOIN_EVENT: {
            const auto* peer_event = reinterpret_cast<const aoonet_client_peer_event*>(event);
            if (peer_event->result > 0 && peer_event->address && !load_remote_endpoint()) {
                RemoteEndpoint endpoint;
                std::memcpy(&endpoint.addr, peer_event->address, static_cast<std::size_t>(peer_event->length));
                endpoint.addr_len = static_cast<socklen_t>(peer_event->length);
                endpoint.user = peer_event->user ? peer_event->user : "unknown";
                std::atomic_store(&remote_endpoint_, std::make_shared<RemoteEndpoint>(endpoint));
                std::vector<NetworkCandidate> remote_candidates;
                remote_candidates.push_back(candidate_gatherer_.make_remote_host_candidate(endpoint.addr, endpoint.addr_len));
                path_manager_.set_remote_candidates(remote_candidates);
                remote_candidates_fetched_ = false;
                next_candidate_fetch_at_ = std::chrono::steady_clock::now();
                try_fetch_remote_candidates(endpoint.user);
                session_token_ = session_token_for_group(options_.group);
                probe_.set_session_token(session_token_);
                elect_timing_authority(endpoint.user);
                PJ_LOG(Info, "peer_joined=" << endpoint.user
                    << " address=" << UdpDatagramSocket::address_to_string(endpoint.addr));
            }
            break;
        }
        default:
            break;
    }
}

std::uint64_t AooPeerClient::session_token_for_group(const std::string& group) {
    return stable_fnv1a_64(group);
}

int AooPeerClient::run() {
    return run_qualification(options_.audio.seconds > 0);
}

int AooPeerClient::run_network_test() {
    return run_qualification(false);
}

void AooPeerClient::print_qualification_report(const PathSelectionReport& path_report, const MeasurementReport& probe_report) const {
    std::cout << "{\n"
              << "  \"network\": " << path_report.to_json() << ",\n"
              << "  \"probe\": " << probe_report.to_json() << ",\n"
              << "  \"session_profile\": " << probe_report.recommendation.to_json() << ",\n"
              << "  \"timing\": " << timing_state_json() << "\n"
              << "}\n";
}

void AooPeerClient::apply_input_session_profile() {
    if (options_.session_profile_in.empty()) {
        return;
    }
    const auto profile = read_session_profile(options_.session_profile_in);
    if (!profile) {
        PJ_LOG(Error, "failed to read session profile from " << options_.session_profile_in);
        return;
    }
    apply_session_profile(*profile, options_.audio);
    audio_.set_options(options_.audio);
}

int AooPeerClient::run_qualification(bool include_audio) {
    if (start() != 0) {
        return 1;
    }

    if (!wait_for_remote_peer()) {
        PJ_LOG(Error, "no remote peer joined the group");
        return 2;
    }

    auto path_report = path_manager_.measure_and_select_active_path(session_token_);
    apply_selected_path(path_report);
    reprobe_running_.store(true);
    reprobe_thread_ = std::thread([this]() { background_reprobe_loop(); });
    const auto remote_endpoint = load_remote_endpoint();
    if (!remote_endpoint) {
        return 2;
    }
    probe_.run_probe_window(socket_, *remote_endpoint);
    const auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < settle_deadline) {
        pump_client_events();
        maybe_send_client_messages();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto probe_report = probe_.finish();
    if (!options_.session_profile_out.empty() && !write_session_profile(options_.session_profile_out, probe_report.recommendation)) {
        PJ_LOG(Error, "failed to write session profile to " << options_.session_profile_out);
    }
    reprobe_running_.store(false);
    if (reprobe_thread_.joinable()) {
        reprobe_thread_.join();
    }
    path_report = path_manager_.current_report();
    if (include_audio && options_.audio.seconds > 0) {
        apply_session_profile(probe_report.recommendation, options_.audio);
        audio_.set_options(options_.audio);
        if (audio_.setup(socket_, &remote_endpoint_) != 0) {
            return 3;
        }
        const auto audio_report = audio_.run({this, &AooPeerClient::tick_callback});
        audio_.teardown();
        std::cout << "{\n"
                  << "  \"network\": " << path_report.to_json() << ",\n"
                  << "  \"probe\": " << probe_report.to_json() << ",\n"
                  << "  \"session_profile\": " << probe_report.recommendation.to_json() << ",\n"
                  << "  \"timing\": " << timing_state_json() << ",\n"
                  << "  \"audio\": " << audio_report.to_json() << "\n"
                  << "}\n";
    } else {
        print_qualification_report(path_report, probe_report);
    }

    return 0;
}

}  // namespace peerjam
