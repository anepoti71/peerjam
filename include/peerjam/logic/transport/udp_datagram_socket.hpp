#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <sys/socket.h>

namespace peerjam {

class UdpDatagramSocket {
  public:
    UdpDatagramSocket() = default;
    ~UdpDatagramSocket();

    UdpDatagramSocket(const UdpDatagramSocket&) = delete;
    UdpDatagramSocket& operator=(const UdpDatagramSocket&) = delete;

    int open(int port);
    int open_bound(const sockaddr_storage& addr, socklen_t addr_len);
    int rebind(const sockaddr_storage& addr, socklen_t addr_len);
    void close();

    [[nodiscard]] int native_handle() const { return fd_.load(); }
    [[nodiscard]] bool is_open() const { return fd_.load() >= 0; }

    int send_to(const sockaddr_storage& addr, socklen_t addr_len, const void* data, std::size_t size) const;
    int receive(void* data, std::size_t size, sockaddr_storage& addr, socklen_t& addr_len) const;

    static std::string address_to_string(const sockaddr_storage& addr);

  private:
    static int create_bound_socket(const sockaddr_storage& addr, socklen_t addr_len, bool preserve_port);

    std::atomic<int> fd_ {-1};
};

}  // namespace peerjam
