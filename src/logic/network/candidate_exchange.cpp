#include "peerjam/logic/network/candidate_exchange.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

namespace peerjam {
namespace {

struct PublishedCandidateSet {
    std::string user;
    std::vector<NetworkCandidate> candidates;
};

std::mutex g_store_mutex;
std::map<std::string, std::map<std::string, std::vector<NetworkCandidate>>> g_candidate_store;

bool resolve_tcp_endpoint(const std::string& host, int port, sockaddr_storage& addr, socklen_t& addr_len) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result) {
        return false;
    }
    bool ok = false;
    for (auto* it = result; it != nullptr; it = it->ai_next) {
        if (it->ai_addrlen <= sizeof(sockaddr_storage)) {
            std::memcpy(&addr, it->ai_addr, static_cast<std::size_t>(it->ai_addrlen));
            addr_len = static_cast<socklen_t>(it->ai_addrlen);
            ok = true;
            break;
        }
    }
    ::freeaddrinfo(result);
    return ok;
}

bool connect_to_host(const std::string& host, int port, int& fd) {
    sockaddr_storage addr {};
    socklen_t addr_len = 0;
    if (!resolve_tcp_endpoint(host, port, addr, addr_len)) {
        return false;
    }
    fd = ::socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) < 0) {
        ::close(fd);
        fd = -1;
        return false;
    }
    return true;
}

bool write_all(int fd, const std::string& text) {
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const auto written = ::send(fd, data, remaining, 0);
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

bool read_line(int fd, std::string& line) {
    line.clear();
    char ch = 0;
    while (true) {
        const auto n = ::recv(fd, &ch, 1, 0);
        if (n <= 0) {
            return !line.empty();
        }
        if (ch == '\n') {
            return true;
        }
        line.push_back(ch);
    }
}

std::string serialize_candidate(const NetworkCandidate& candidate) {
    const auto family = candidate.is_ipv6 ? AF_INET6 : AF_INET;
    std::string ip;
    int port = 0;
    char buffer[INET6_ADDRSTRLEN] {};
    if (candidate.addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&candidate.addr);
        ::inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer));
        ip = buffer;
        port = ntohs(in->sin_port);
    } else if (candidate.addr.ss_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&candidate.addr);
        ::inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer));
        ip = buffer;
        port = ntohs(in6->sin6_port);
    }
    std::ostringstream os;
    os << "C "
       << candidate_type_name(candidate.type) << " "
       << candidate.foundation << " "
       << candidate.interface_name << " "
       << family << " "
       << ip << " "
       << port << " "
       << candidate.priority << " "
       << (candidate.is_loopback ? 1 : 0) << "\n";
    return os.str();
}

bool parse_candidate_line(const std::string& line, NetworkCandidate& candidate) {
    std::istringstream is(line);
    std::string prefix;
    std::string type_name;
    int family = AF_INET;
    std::string ip;
    int port = 0;
    int loopback = 0;
    if (!(is >> prefix >> type_name >> candidate.foundation >> candidate.interface_name >> family >> ip >> port >> candidate.priority >> loopback)) {
        return false;
    }
    if (prefix != "C") {
        return false;
    }
    if (type_name == "host") {
        candidate.type = CandidateType::Host;
    } else if (type_name == "srflx") {
        candidate.type = CandidateType::ServerReflexive;
    } else {
        candidate.type = CandidateType::Relay;
    }
    candidate.is_ipv6 = family == AF_INET6;
    candidate.is_loopback = loopback != 0;
    return parse_candidate_address(family, ip, port, candidate.addr, candidate.addr_len);
}

int make_listen_socket(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

bool parse_candidate_address(int family, const std::string& ip, int port, sockaddr_storage& addr, socklen_t& addr_len) {
    std::memset(&addr, 0, sizeof(addr));
    if (family == AF_INET) {
        auto* in = reinterpret_cast<sockaddr_in*>(&addr);
        in->sin_family = AF_INET;
        in->sin_port = htons(static_cast<std::uint16_t>(port));
        if (::inet_pton(AF_INET, ip.c_str(), &in->sin_addr) != 1) {
            return false;
        }
        addr_len = sizeof(sockaddr_in);
        return true;
    }
    if (family == AF_INET6) {
        auto* in6 = reinterpret_cast<sockaddr_in6*>(&addr);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(static_cast<std::uint16_t>(port));
        if (::inet_pton(AF_INET6, ip.c_str(), &in6->sin6_addr) != 1) {
            return false;
        }
        addr_len = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

bool CandidateExchangeClient::publish(
    const std::string& host,
    int port,
    const std::string& session,
    const std::string& user,
    const std::vector<NetworkCandidate>& candidates
) const {
    int fd = -1;
    if (!connect_to_host(host, port, fd)) {
        return false;
    }
    std::ostringstream request;
    request << "PUBLISH " << session << " " << user << "\n";
    for (const auto& candidate : candidates) {
        request << serialize_candidate(candidate);
    }
    request << "DONE\n";
    const bool ok = write_all(fd, request.str());
    ::close(fd);
    return ok;
}

std::optional<std::vector<NetworkCandidate>> CandidateExchangeClient::fetch(
    const std::string& host,
    int port,
    const std::string& session,
    const std::string& user,
    const std::string& peer_user
) const {
    int fd = -1;
    if (!connect_to_host(host, port, fd)) {
        return std::nullopt;
    }
    std::ostringstream request;
    request << "FETCH " << session << " " << user << " " << peer_user << "\n";
    if (!write_all(fd, request.str())) {
        ::close(fd);
        return std::nullopt;
    }

    std::vector<NetworkCandidate> candidates;
    std::string line;
    if (!read_line(fd, line)) {
        ::close(fd);
        return std::nullopt;
    }
    if (line == "NONE") {
        ::close(fd);
        return std::nullopt;
    }
    if (line != "OK") {
        ::close(fd);
        return std::nullopt;
    }
    while (read_line(fd, line)) {
        if (line == "DONE") {
            break;
        }
        NetworkCandidate candidate;
        if (parse_candidate_line(line, candidate)) {
            candidates.push_back(candidate);
        }
    }
    ::close(fd);
    return candidates;
}

CandidateExchangeServer::~CandidateExchangeServer() {
    stop();
}

bool CandidateExchangeServer::start(int port) {
    listen_fd_ = make_listen_socket(port);
    if (listen_fd_ < 0) {
        return false;
    }
    running_ = true;
    std::thread([this]() { run(); }).detach();
    return true;
}

void CandidateExchangeServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void CandidateExchangeServer::run() {
    while (running_) {
        sockaddr_storage addr {};
        socklen_t addr_len = sizeof(addr);
        const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
        if (fd < 0) {
            if (running_) {
                continue;
            }
            break;
        }
        handle_client(fd);
        ::close(fd);
    }
}

void CandidateExchangeServer::handle_client(int fd) {
    std::string line;
    if (!read_line(fd, line)) {
        return;
    }
    std::istringstream header(line);
    std::string command;
    header >> command;
    if (command == "PUBLISH") {
        std::string session;
        std::string user;
        if (!(header >> session >> user)) {
            return;
        }
        std::vector<NetworkCandidate> candidates;
        while (read_line(fd, line)) {
            if (line == "DONE") {
                break;
            }
            NetworkCandidate candidate;
            if (parse_candidate_line(line, candidate)) {
                candidates.push_back(candidate);
            }
        }
        std::lock_guard lock(g_store_mutex);
        g_candidate_store[session][user] = std::move(candidates);
        write_all(fd, "OK\n");
        return;
    }
    if (command == "FETCH") {
        std::string session;
        std::string user;
        std::string peer_user;
        if (!(header >> session >> user >> peer_user)) {
            return;
        }
        std::vector<NetworkCandidate> candidates;
        {
            std::lock_guard lock(g_store_mutex);
            const auto session_it = g_candidate_store.find(session);
            if (session_it == g_candidate_store.end()) {
                write_all(fd, "NONE\n");
                return;
            }
            const auto peer_it = session_it->second.find(peer_user);
            if (peer_it == session_it->second.end()) {
                write_all(fd, "NONE\n");
                return;
            }
            candidates = peer_it->second;
        }
        std::ostringstream response;
        response << "OK\n";
        for (const auto& candidate : candidates) {
            response << serialize_candidate(candidate);
        }
        response << "DONE\n";
        write_all(fd, response.str());
    }
}

}  // namespace peerjam
