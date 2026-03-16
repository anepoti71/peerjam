#include "peerjam/logic/ui/realtime_ui_server.hpp"

#include "peerjam/common/logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace peerjam {
namespace {

constexpr int kAcceptPollMs = 200;
constexpr int kRequestReadBytes = 2048;

std::string cors_headers(const std::string& content_type, std::size_t size) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: Content-Type\r\n"
           "Content-Length: " + std::to_string(size) + "\r\n\r\n";
}

void send_simple_response(int fd, const std::string& status, const std::string& body) {
    const std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n"
        + body;
    ::send(fd, response.data(), response.size(), 0);
}

bool parse_body_index(const std::string& body, const char* key, int& out_value) {
    const std::string token = std::string("\"") + key + "\"";
    const auto k = body.find(token);
    if (k == std::string::npos) {
        return false;
    }
    const auto colon = body.find(':', k + token.size());
    if (colon == std::string::npos) {
        return false;
    }
    char* end_ptr = nullptr;
    const long value = std::strtol(body.c_str() + static_cast<long>(colon + 1), &end_ptr, 10);
    if (end_ptr == body.c_str() + static_cast<long>(colon + 1)) {
        return false;
    }
    out_value = static_cast<int>(value);
    return true;
}

int create_listen_socket(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

RealtimeUiServer::~RealtimeUiServer() {
    stop();
}

bool RealtimeUiServer::start(int port) {
    if (running_.load() || port <= 0) {
        return false;
    }
    listen_fd_ = create_listen_socket(port);
    if (listen_fd_ < 0) {
        PJ_LOG(Error, "ui telemetry server failed to bind port " << port << " errno=" << errno);
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this]() { accept_loop(); });
    writer_thread_ = std::thread([this]() { writer_loop(); });
    PJ_LOG(Info, "ui telemetry server listening on http://127.0.0.1:" << port << "/events");
    return true;
}

void RealtimeUiServer::stop() {
    running_.store(false);
    cv_.notify_all();
    {
        std::lock_guard lock(mutex_);
        close_client_locked();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

void RealtimeUiServer::publish_json(std::string payload) {
    if (!running_.load()) {
        return;
    }
    std::lock_guard lock(mutex_);
    pending_payload_ = std::move(payload);
    pending_ = true;
    cv_.notify_one();
}

void RealtimeUiServer::accept_loop() {
    while (running_.load()) {
        pollfd pfd {};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, kAcceptPollMs) <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            continue;
        }
        if (!attach_event_client(fd)) {
            ::close(fd);
        }
    }
}

bool RealtimeUiServer::attach_event_client(int fd) {
    char buffer[kRequestReadBytes] {};
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return false;
    }

    const std::string request(buffer, static_cast<std::size_t>(n));
    const bool is_options = request.rfind("OPTIONS ", 0) == 0;
    const bool wants_events = request.rfind("GET /events", 0) == 0;
    const bool wants_devices = request.rfind("GET /devices", 0) == 0;
    const bool sets_devices = request.rfind("POST /devices", 0) == 0;

    if (is_options) {
        static constexpr const char* kOptions =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n\r\n";
        ::send(fd, kOptions, std::strlen(kOptions), 0);
        return false;
    }

    if (wants_devices) {
        std::string body;
        {
            std::lock_guard lock(mutex_);
            body = devices_payload_;
        }
        const std::string headers = cors_headers("application/json", body.size());
        ::send(fd, headers.data(), headers.size(), 0);
        ::send(fd, body.data(), body.size(), 0);
        return false;
    }

    if (sets_devices) {
        int input_index = -2;
        int output_index = -2;
        (void)parse_body_index(request, "input_device_index", input_index);
        (void)parse_body_index(request, "output_device_index", output_index);
        {
            std::lock_guard lock(mutex_);
            selected_input_device_ = input_index;
            selected_output_device_ = output_index;
        }
        send_simple_response(fd, "200 OK", "{\"status\":\"ok\"}");
        return false;
    }

    if (!wants_events) {
        static constexpr const char* kNotFound =
            "HTTP/1.1 404 Not Found\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n";
        ::send(fd, kNotFound, std::strlen(kNotFound), 0);
        return false;
    }

    static constexpr const char* kHeaders =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n";
    if (::send(fd, kHeaders, std::strlen(kHeaders), 0) <= 0) {
        return false;
    }

    std::lock_guard lock(mutex_);
    close_client_locked();
    client_fd_ = fd;
    return true;
}

void RealtimeUiServer::writer_loop() {
    while (running_.load()) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() { return !running_.load() || pending_; });
        if (!running_.load()) {
            return;
        }
        if (!pending_ || client_fd_ < 0) {
            continue;
        }

        const std::string frame = "data: " + pending_payload_ + "\n\n";
        pending_ = false;
        const int fd = client_fd_;
        lock.unlock();

        const ssize_t sent = ::send(fd, frame.data(), frame.size(), 0);
        if (sent <= 0) {
            std::lock_guard relock(mutex_);
            close_client_locked();
        }
    }
}

void RealtimeUiServer::close_client_locked() {
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
}

void RealtimeUiServer::set_devices_json(std::string payload) {
    std::lock_guard lock(mutex_);
    devices_payload_ = std::move(payload);
}

bool RealtimeUiServer::consume_selected_devices(int& input_device_index, int& output_device_index) {
    std::lock_guard lock(mutex_);
    if (selected_input_device_ == -2 && selected_output_device_ == -2) {
        return false;
    }
    input_device_index = selected_input_device_;
    output_device_index = selected_output_device_;
    selected_input_device_ = -2;
    selected_output_device_ = -2;
    return true;
}

}  // namespace peerjam
