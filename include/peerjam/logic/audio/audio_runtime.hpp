#pragma once

#include "aoo/aoo.h"
#include "peerjam/logic/audio/audio_stream.hpp"
#include "peerjam/logic/control/peer_types.hpp"
#include "peerjam/logic/transport/udp_datagram_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
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

    void set_options(const AudioOptions& options);
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

    bool set_source_codec(const std::string& codec);
    void sync_tracker_options();
    void maybe_switch_runtime_codec(std::chrono::steady_clock::time_point now, float buffer_fill);
    void handle_source_event(const aoo_event* event);
    void handle_sink_event(const aoo_event* event);
    void maybe_adjust_drift_control(std::chrono::steady_clock::time_point now);
    void maybe_adjust_sink_buffer(float buffer_fill, std::chrono::steady_clock::time_point now);
    AudioStreamReport run_synthetic(TickHook tick_hook);
    AudioStreamReport run_device_io(TickHook tick_hook);

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
    int current_sink_buffer_ms_ {};
    int max_sink_buffer_ms_ {};
    int low_fill_streak_ {};
    int high_fill_streak_ {};
    std::uint32_t last_codec_eval_lost_blocks_ {};
    std::uint32_t last_codec_eval_gap_blocks_ {};
    std::chrono::steady_clock::time_point next_codec_eval_at_ {};
    std::chrono::steady_clock::time_point codec_stable_since_ {};
    std::string active_codec_ {"pcm"};
    std::chrono::steady_clock::time_point next_drift_adjust_at_ {};
    std::chrono::steady_clock::time_point next_buffer_adjust_at_ {};
};

}  // namespace peerjam
