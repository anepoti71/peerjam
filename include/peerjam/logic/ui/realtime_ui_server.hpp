#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace peerjam {

class RealtimeUiServer {
  public:
    RealtimeUiServer() = default;
    ~RealtimeUiServer();

    RealtimeUiServer(const RealtimeUiServer&) = delete;
    RealtimeUiServer& operator=(const RealtimeUiServer&) = delete;

    bool start(int port);
    void stop();
    void publish_json(std::string payload);
    void set_devices_json(std::string payload);
    bool consume_selected_devices(int& input_device_index, int& output_device_index);

  private:
    void accept_loop();
    void writer_loop();
    bool attach_event_client(int fd);
    void close_client_locked();

    std::atomic<bool> running_ {false};
    int listen_fd_ {-1};
    int client_fd_ {-1};
    std::thread accept_thread_;
    std::thread writer_thread_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::string pending_payload_;
    std::string devices_payload_ {"[]"};
    bool pending_ {false};
    int selected_input_device_ {-2};
    int selected_output_device_ {-2};
};

}  // namespace peerjam
