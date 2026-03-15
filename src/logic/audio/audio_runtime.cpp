#include "peerjam/logic/audio/audio_runtime.hpp"

#include "aoo/aoo_pcm.h"
#include "aoo/aoo_types.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace peerjam {

AudioRuntime::AudioRuntime(const AudioOptions& options)
    : options_(options)
    , tracker_(options) {}

AudioRuntime::~AudioRuntime() {
    teardown();
}

int AudioRuntime::setup(const UdpDatagramSocket& socket, const std::shared_ptr<RemoteEndpoint>* endpoint_ref) {
    if (options_.seconds <= 0) {
        return 0;
    }

    reply_context_.socket = &socket;
    reply_context_.endpoint_ref = endpoint_ref;

    source_ = aoo_source_new(options_.source_id);
    sink_ = aoo_sink_new(options_.sink_id);
    if (!source_ || !sink_) {
        return 1;
    }

    if (aoo_source_setup(source_, options_.sample_rate_hz, options_.frame_samples, options_.channels) <= 0
        || aoo_sink_setup(sink_, options_.sample_rate_hz, options_.frame_samples, options_.channels) <= 0) {
        return 1;
    }

    aoo_format_pcm format {};
    format.header.codec = AOO_CODEC_PCM;
    format.header.nchannels = options_.channels;
    format.header.samplerate = options_.sample_rate_hz;
    format.header.blocksize = options_.frame_samples;
    format.bitdepth = AOO_PCM_FLOAT32;
    aoo_source_set_format(source_, &format.header);

    const int32_t dynamic_resampling = options_.dynamic_resampling ? 1 : 0;
    aoo_source_set_buffersize(source_, options_.source_buffer_ms);
    aoo_source_set_packetsize(source_, options_.packetsize);
    aoo_source_set_resend_buffersize(source_, options_.resend_buffer_ms);
    aoo_source_set_redundancy(source_, options_.redundancy);
    aoo_source_set_ping_interval(source_, 250);
    aoo_source_set_option(source_, aoo_opt_dynamic_resampling, (void*)&dynamic_resampling, sizeof(dynamic_resampling));

    aoo_sink_set_buffersize(sink_, options_.sink_buffer_ms);
    aoo_sink_set_packetsize(sink_, options_.packetsize);
    aoo_sink_set_resend_limit(sink_, options_.resend_limit);
    aoo_sink_set_resend_interval(sink_, options_.resend_interval_ms);
    aoo_sink_set_resend_maxnumframes(sink_, options_.resend_max_frames);
    aoo_sink_set_option(sink_, aoo_opt_dynamic_resampling, (void*)&dynamic_resampling, sizeof(dynamic_resampling));

    aoo_source_add_sink(source_, &reply_context_, options_.sink_id, &AudioRuntime::reply_callback);
    aoo_sink_invite_source(sink_, &reply_context_, options_.source_id, &AudioRuntime::reply_callback);
    aoo_source_start(source_);

    source_channels_.assign(static_cast<std::size_t>(options_.channels), std::vector<float>(static_cast<std::size_t>(options_.frame_samples), 0.0f));
    sink_channels_.assign(static_cast<std::size_t>(options_.channels), std::vector<float>(static_cast<std::size_t>(options_.frame_samples), 0.0f));
    source_channel_ptrs_.clear();
    sink_channel_ptrs_.clear();
    for (int ch = 0; ch < options_.channels; ++ch) {
        source_channel_ptrs_.push_back(source_channels_[static_cast<std::size_t>(ch)].data());
        sink_channel_ptrs_.push_back(sink_channels_[static_cast<std::size_t>(ch)].data());
    }
    sink_interleaved_.assign(static_cast<std::size_t>(options_.frame_samples * options_.channels), 0.0f);
    tone_phase_ = 0.0;
    return 0;
}

void AudioRuntime::teardown() {
    source_channel_ptrs_.clear();
    sink_channel_ptrs_.clear();
    source_channels_.clear();
    sink_channels_.clear();
    sink_interleaved_.clear();
    if (source_) {
        aoo_source_stop(source_);
        aoo_source_free(source_);
        source_ = nullptr;
    }
    if (sink_) {
        aoo_sink_free(sink_);
        sink_ = nullptr;
    }
}

bool AudioRuntime::handle_datagram(const std::uint8_t* data, std::size_t size) {
    if (!source_ || !sink_) {
        return false;
    }
    int32_t aoo_type = 0;
    int32_t aoo_id = 0;
    if (aoo_parse_pattern(reinterpret_cast<const char*>(data), static_cast<int32_t>(size), &aoo_type, &aoo_id) <= 0) {
        return false;
    }
    if (aoo_type == AOO_TYPE_SOURCE) {
        aoo_source_handle_message(source_, reinterpret_cast<const char*>(data), static_cast<int32_t>(size), &reply_context_, &AudioRuntime::reply_callback);
        return true;
    }
    if (aoo_type == AOO_TYPE_SINK) {
        aoo_sink_handle_message(sink_, reinterpret_cast<const char*>(data), static_cast<int32_t>(size), &reply_context_, &AudioRuntime::reply_callback);
        return true;
    }
    return false;
}

AudioStreamReport AudioRuntime::run(TickHook tick_hook) {
    if (!source_ || !sink_) {
        return {};
    }

    const double pi = std::acos(-1.0);
    const double phase_inc = 2.0 * pi * options_.tone_hz / static_cast<double>(options_.sample_rate_hz);
    const auto block_period = std::chrono::duration<double>(
        static_cast<double>(options_.frame_samples) / static_cast<double>(options_.sample_rate_hz)
    );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options_.seconds);
    auto next_block = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < options_.frame_samples; ++i) {
            const float sample = static_cast<float>(std::sin(tone_phase_) * options_.tone_level);
            tone_phase_ += phase_inc;
            if (tone_phase_ >= 2.0 * pi) {
                tone_phase_ -= 2.0 * pi;
            }
            for (int ch = 0; ch < options_.channels; ++ch) {
                source_channels_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = sample;
            }
        }

        const auto ntp_now = aoo_osctime_get();
        const auto steady_now_ns = now_ns();
        aoo_source_process(source_, reinterpret_cast<const aoo_sample**>(source_channel_ptrs_.data()), options_.frame_samples, ntp_now);
        aoo_source_send(source_);
        aoo_sink_process(sink_, reinterpret_cast<aoo_sample**>(sink_channel_ptrs_.data()), options_.frame_samples, ntp_now);
        aoo_sink_send(sink_);

        for (int i = 0; i < options_.frame_samples; ++i) {
            for (int ch = 0; ch < options_.channels; ++ch) {
                sink_interleaved_[static_cast<std::size_t>(i * options_.channels + ch)] =
                    sink_channels_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            }
        }
        tracker_.on_render_block(sink_interleaved_.data(), options_.frame_samples, options_.channels, steady_now_ns);

        float buffer_fill = 0.0f;
        if (aoo_sink_get_sourceoption(sink_, &reply_context_, options_.source_id, aoo_opt_buffer_fill_ratio, &buffer_fill, sizeof(buffer_fill)) > 0) {
            tracker_.on_buffer_fill(buffer_fill, steady_now_ns);
        }

        aoo_source_handle_events(source_, &AudioRuntime::source_event_handler, this);
        aoo_sink_handle_events(sink_, &AudioRuntime::sink_event_handler, this);
        if (tick_hook.fn) {
            tick_hook.fn(tick_hook.user);
        }

        next_block += std::chrono::duration_cast<std::chrono::steady_clock::duration>(block_period);
        std::this_thread::sleep_until(next_block);
    }

    return tracker_.finish();
}

int32_t AudioRuntime::reply_callback(void* user, const char* data, int32_t size) {
    auto* context = static_cast<ReplyContext*>(user);
    if (!context || !context->socket || !context->endpoint_ref) {
        return -1;
    }
    const auto endpoint = std::atomic_load(context->endpoint_ref);
    if (!endpoint) {
        return -1;
    }
    return context->socket->send_to(endpoint->addr, endpoint->addr_len, data, static_cast<std::size_t>(size));
}

int32_t AudioRuntime::source_event_handler(void* user, const aoo_event** events, int32_t count) {
    auto* self = static_cast<AudioRuntime*>(user);
    if (!self) {
        return 0;
    }
    for (int32_t i = 0; i < count; ++i) {
        self->handle_source_event(events[i]);
    }
    return count;
}

int32_t AudioRuntime::sink_event_handler(void* user, const aoo_event** events, int32_t count) {
    auto* self = static_cast<AudioRuntime*>(user);
    if (!self) {
        return 0;
    }
    for (int32_t i = 0; i < count; ++i) {
        self->handle_sink_event(events[i]);
    }
    return count;
}

std::uint64_t AudioRuntime::now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

void AudioRuntime::handle_source_event(const aoo_event* event) {
    (void)event;
}

void AudioRuntime::handle_sink_event(const aoo_event* event) {
    if (!event) {
        return;
    }
    switch (event->type) {
        case AOO_SOURCE_STATE_EVENT: {
            const auto* state = reinterpret_cast<const aoo_source_state_event*>(event);
            if (state->state == AOO_SOURCE_STATE_PLAY) {
                tracker_.on_play_event();
            } else {
                tracker_.on_stop_event();
            }
            break;
        }
        case AOO_BLOCK_LOST_EVENT:
            tracker_.on_lost_blocks(static_cast<std::uint32_t>(reinterpret_cast<const aoo_block_lost_event*>(event)->count));
            break;
        case AOO_BLOCK_REORDERED_EVENT:
            tracker_.on_reordered_blocks(static_cast<std::uint32_t>(reinterpret_cast<const aoo_block_reordered_event*>(event)->count));
            break;
        case AOO_BLOCK_RESENT_EVENT:
            tracker_.on_resent_blocks(static_cast<std::uint32_t>(reinterpret_cast<const aoo_block_resent_event*>(event)->count));
            break;
        case AOO_BLOCK_GAP_EVENT:
            tracker_.on_gap_blocks(static_cast<std::uint32_t>(reinterpret_cast<const aoo_block_gap_event*>(event)->count));
            break;
        default:
            break;
    }
}

}  // namespace peerjam
