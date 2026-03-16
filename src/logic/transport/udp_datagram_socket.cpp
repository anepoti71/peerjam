#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace peerjam {

UdpDatagramSocket::~UdpDatagramSocket() {
    close();
}

namespace {

int make_socket(int family) {
    return ::socket(family, SOCK_DGRAM, 0);
}

void apply_reuse_flags(int fd) {
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
}

bool make_nonblocking(int fd) {
    int nonblocking = 1;
    return ::ioctl(fd, FIONBIO, &nonblocking) >= 0;
}

}  // namespace

int UdpDatagramSocket::create_bound_socket(const sockaddr_storage& addr, socklen_t addr_len, bool preserve_port) {
    const int fd = make_socket(addr.ss_family);
    if (fd < 0) {
        return -1;
    }
    apply_reuse_flags(fd);

    sockaddr_storage bind_addr = addr;
    if (!preserve_port) {
        if (bind_addr.ss_family == AF_INET) {
            reinterpret_cast<sockaddr_in*>(&bind_addr)->sin_port = 0;
        } else if (bind_addr.ss_family == AF_INET6) {
            reinterpret_cast<sockaddr_in6*>(&bind_addr)->sin6_port = 0;
        }
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), addr_len) < 0) {
        ::close(fd);
        return -1;
    }

    if (!make_nonblocking(fd)) {
        ::close(fd);
        return -1;
    }

    return fd;
}

int UdpDatagramSocket::open(int port) {
    std::unique_lock lock(fd_mutex_);
    sockaddr_storage addr {};
    auto* in = reinterpret_cast<sockaddr_in*>(&addr);
    in->sin_family = AF_INET;
    in->sin_port = htons(static_cast<std::uint16_t>(port));
    in->sin_addr.s_addr = htonl(INADDR_ANY);
    const int fd = create_bound_socket(addr, sizeof(sockaddr_in), true);
    if (fd < 0) {
        return 1;
    }
    const int old_fd = fd_.exchange(fd);
    if (old_fd >= 0) {
        ::close(old_fd);
    }
    return 0;
}

int UdpDatagramSocket::open_bound(const sockaddr_storage& addr, socklen_t addr_len) {
    std::unique_lock lock(fd_mutex_);
    // Bind to the candidate interface/address but allow an ephemeral port.
    // This avoids SO_REUSEPORT fan-out with the main runtime socket when both
    // sockets would otherwise share the same UDP port.
    const int fd = create_bound_socket(addr, addr_len, false);
    if (fd < 0) {
        return 1;
    }
    const int old_fd = fd_.exchange(fd);
    if (old_fd >= 0) {
        ::close(old_fd);
    }
    return 0;
}

int UdpDatagramSocket::rebind(const sockaddr_storage& addr, socklen_t addr_len) {
    std::unique_lock lock(fd_mutex_);
    const int fd = create_bound_socket(addr, addr_len, true);
    if (fd < 0) {
        return 1;
    }
    const int old_fd = fd_.exchange(fd);
    if (old_fd >= 0) {
        ::close(old_fd);
    }
    return 0;
}

void UdpDatagramSocket::close() {
    std::unique_lock lock(fd_mutex_);
    const int fd = fd_.exchange(-1);
    if (fd >= 0) {
        ::close(fd);
    }
}

int UdpDatagramSocket::send_to(const sockaddr_storage& addr, socklen_t addr_len, const void* data, std::size_t size) const {
    std::shared_lock lock(fd_mutex_);
    if (!data || size == 0) {
        return -1;
    }
    int fd = fd_.load();
    if (fd < 0) {
        return -1;
    }
    return static_cast<int>(::sendto(fd, data, size, 0, reinterpret_cast<const sockaddr*>(&addr), addr_len));
}

int UdpDatagramSocket::receive(void* data, std::size_t size, sockaddr_storage& addr, socklen_t& addr_len) const {
    std::shared_lock lock(fd_mutex_);
    if (!data || size == 0) {
        return -1;
    }
    int fd = fd_.load();
    if (fd < 0) {
        return -1;
    }
    addr_len = sizeof(addr);
    return static_cast<int>(::recvfrom(fd, data, size, 0, reinterpret_cast<sockaddr*>(&addr), &addr_len));
}

std::string UdpDatagramSocket::address_to_string(const sockaddr_storage& addr) {
    char buffer[INET6_ADDRSTRLEN] {};
    if (addr.ss_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&addr);
        ::inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer));
        return std::string(buffer) + ":" + std::to_string(ntohs(in->sin_port));
    }
    return "unknown";
}

}  // namespace peerjam
