#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
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

int UdpDatagramSocket::open(int port) {
    fd_ = make_socket(AF_INET);
    if (fd_ < 0) {
        return 1;
    }
    apply_reuse_flags(fd_);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return 1;
    }

    if (!make_nonblocking(fd_)) {
        return 1;
    }

    return 0;
}

int UdpDatagramSocket::open_bound(const sockaddr_storage& addr, socklen_t addr_len) {
    fd_ = make_socket(addr.ss_family);
    if (fd_ < 0) {
        return 1;
    }
    apply_reuse_flags(fd_);

    sockaddr_storage bind_addr = addr;
    if (bind_addr.ss_family == AF_INET) {
        reinterpret_cast<sockaddr_in*>(&bind_addr)->sin_port = 0;
    } else if (bind_addr.ss_family == AF_INET6) {
        reinterpret_cast<sockaddr_in6*>(&bind_addr)->sin6_port = 0;
    }

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&bind_addr), addr_len) < 0) {
        return 1;
    }
    if (!make_nonblocking(fd_)) {
        return 1;
    }
    return 0;
}

void UdpDatagramSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int UdpDatagramSocket::send_to(const sockaddr_storage& addr, socklen_t addr_len, const void* data, std::size_t size) const {
    if (fd_ < 0 || !data || size == 0) {
        return -1;
    }
    return static_cast<int>(::sendto(fd_, data, size, 0, reinterpret_cast<const sockaddr*>(&addr), addr_len));
}

int UdpDatagramSocket::receive(void* data, std::size_t size, sockaddr_storage& addr, socklen_t& addr_len) const {
    if (fd_ < 0 || !data || size == 0) {
        return -1;
    }
    addr_len = sizeof(addr);
    return static_cast<int>(::recvfrom(fd_, data, size, 0, reinterpret_cast<sockaddr*>(&addr), &addr_len));
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
