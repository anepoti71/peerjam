#pragma once

#include "aoo/aoo.h"
#include "peerjam/logic/audio/audio_stream.hpp"
#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace peerjam {

class AudioRuntime {
  public:
    struct TickHook {
        void* user {};
        void (*fn)(void*) {nullptr};
    };

    explicit AudioRuntime(const AudioOptions& options);
    ~AudioRuntime();

    AudioRuntime(const AudioRuntime&) = delete;
    AudioRuntime& operator=(const AudioRuntime&) = delete;

    void set_options(const AudioOptions& options) {
        options_ = options;
        tracker_.set_options(options);
    }
    int setup(const UdpDatagramSocket& socket, const std::shared_ptr<RemoteEndpoint>* endpoint_ref);
    void teardown();
    bool handle_datagram(const std::uint8_t* data, std::size_t size);
    AudioStreamReport run(TickHook tick_hook);

  private:
    struct ReplyContext {
        const UdpDatagramSocket* socket {};
        const std::shared_ptr<RemoteEndpoint>* endpoint_ref {};
    };

    static int32_t reply_callback(void* user, const char* data, int32_t size);
    static int32_t source_event_handler(void* user, const aoo_event** events, int32_t count);
    static int32_t sink_event_handler(void* user, const aoo_event** events, int32_t count);
    static std::uint64_t now_ns();

    void handle_source_event(const aoo_event* event);
    void handle_sink_event(const aoo_event* event);

    AudioOptions options_;
    ReplyContext reply_context_;
    AudioStreamTracker tracker_;
    aoo_source* source_ {nullptr};
    aoo_sink* sink_ {nullptr};
    std::vector<std::vector<float>> source_channels_;
    std::vector<const float*> source_channel_ptrs_;
    std::vector<std::vector<float>> sink_channels_;
    std::vector<float*> sink_channel_ptrs_;
    std::vector<float> sink_interleaved_;
    double tone_phase_ {};
};

}  // namespace peerjam
