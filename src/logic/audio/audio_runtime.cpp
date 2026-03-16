#include "peerjam/logic/audio/audio_runtime.hpp"

#include "peerjam/common/logger.hpp"
#include "aoo/aoo_opus.h"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_types.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace peerjam {
namespace {
constexpr auto kUiSnapshotInterval = std::chrono::milliseconds(100);

bool use_opus_codec(const std::string& codec) {
    return codec == "opus-lowdelay" || codec == "opus";
}

float drift_to_timefilter_bandwidth(double drift_ppm) {
    // Map larger estimated drift to a wider DLL bandwidth so AOO can converge faster.
    const double abs_ppm = std::abs(drift_ppm);
    if (abs_ppm < 20.0) {
        return 0.0015f;
    }
    if (abs_ppm < 80.0) {
        return 0.0030f;
    }
    if (abs_ppm < 200.0) {
        return 0.0060f;
    }
    return 0.0100f;
}

}

AudioRuntime::AudioRuntime(const AudioOptions& options)
    : options_(options)
    , tracker_(options) {}

AudioRuntime::~AudioRuntime() {
    teardown();
}

void AudioRuntime::set_options(const AudioOptions& options) {
    options_ = options;
    if (!source_) {
        active_codec_ = options.codec;
    }
    sync_tracker_options();
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

    if (!set_source_codec(options_.codec)) {
        return 1;
    }

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
    current_sink_buffer_ms_ = options_.sink_buffer_ms;
    max_sink_buffer_ms_ = options_.sink_buffer_ms;
    low_fill_streak_ = 0;
    high_fill_streak_ = 0;
    const auto now = std::chrono::steady_clock::now();
    next_drift_adjust_at_ = now;
    next_buffer_adjust_at_ = now;
    next_codec_eval_at_ = now;
    codec_stable_since_ = now;
    last_codec_eval_lost_blocks_ = 0;
    last_codec_eval_gap_blocks_ = 0;
    next_snapshot_at_ = now;
    has_snapshot_ = false;

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
    capture_source_ = make_audio_capture_source(options_);
    playback_sink_ = make_audio_playback_sink(options_);
    if (!capture_source_) {
        return 1;
    }
    if (!capture_source_->start(options_)) {
        if (options_.device_io_enabled) {
            PJ_LOG(Info, "audio capture source failed to start; falling back to synthetic input");
            AudioOptions fallback = options_;
            fallback.device_io_enabled = false;
            capture_source_ = make_audio_capture_source(fallback);
            if (!capture_source_ || !capture_source_->start(fallback)) {
                return 1;
            }
        } else {
            return 1;
        }
    }
    if (playback_sink_ && !playback_sink_->start(options_)) {
        PJ_LOG(Info, "audio playback sink failed to start; received audio playback disabled");
        playback_sink_.reset();
    }
    return 0;
}

void AudioRuntime::teardown() {
    source_channel_ptrs_.clear();
    sink_channel_ptrs_.clear();
    {
        std::lock_guard lock(snapshot_mutex_);
        has_snapshot_ = false;
    }
    if (capture_source_) {
        capture_source_->stop();
        capture_source_.reset();
    }
    if (playback_sink_) {
        playback_sink_->stop();
        playback_sink_.reset();
    }
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
    return run_synthetic(tick_hook);
}

AudioStreamReport AudioRuntime::run_synthetic(TickHook tick_hook) {
    if (!source_ || !sink_) {
        return {};
    }

    std::vector<float> input_interleaved(static_cast<std::size_t>(options_.frame_samples * options_.channels), 0.0f);
    const auto block_period = std::chrono::duration<double>(
        static_cast<double>(options_.frame_samples) / static_cast<double>(options_.sample_rate_hz)
    );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options_.seconds);
    auto next_block = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline) {
        if (!capture_source_ || !capture_source_->read_interleaved(input_interleaved.data(), options_.frame_samples, options_.channels)) {
            break;
        }
        float input_sum_sq = 0.0f;
        float input_peak = 0.0f;
        for (float sample : input_interleaved) {
            const float value = sample;
            input_sum_sq += value * value;
            input_peak = std::max(input_peak, std::abs(value));
        }
        const float input_rms = input_interleaved.empty() ? 0.0f
            : std::sqrt(input_sum_sq / static_cast<float>(input_interleaved.size()));
        for (int i = 0, idx = 0; i < options_.frame_samples; ++i) {
            for (int ch = 0; ch < options_.channels; ++ch, ++idx) {
                source_channels_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = input_interleaved[static_cast<std::size_t>(idx)];
            }
        }

        const auto ntp_now = aoo_osctime_get();
        const auto steady_now_ns = now_ns();
        aoo_source_process(source_, reinterpret_cast<const aoo_sample**>(source_channel_ptrs_.data()), options_.frame_samples, ntp_now);
        aoo_source_send(source_);
        aoo_sink_process(sink_, reinterpret_cast<aoo_sample**>(sink_channel_ptrs_.data()), options_.frame_samples, ntp_now);
        aoo_sink_send(sink_);

        for (int i = 0, idx = 0; i < options_.frame_samples; ++i) {
            for (int ch = 0; ch < options_.channels; ++ch, ++idx) {
                sink_interleaved_[static_cast<std::size_t>(idx)] = sink_channels_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            }
        }
        if (playback_sink_) {
            (void)playback_sink_->write_interleaved(sink_interleaved_.data(), options_.frame_samples, options_.channels);
        }
        tracker_.on_render_block(sink_interleaved_.data(), options_.frame_samples, options_.channels, steady_now_ns);

        float buffer_fill = 0.0f;
        const auto now = std::chrono::steady_clock::now();
        if (aoo_sink_get_sourceoption(sink_, &reply_context_, options_.source_id, aoo_opt_buffer_fill_ratio, &buffer_fill, sizeof(buffer_fill)) > 0) {
            tracker_.on_buffer_fill(buffer_fill, steady_now_ns);
            maybe_adjust_sink_buffer(buffer_fill, now);
            maybe_switch_runtime_codec(now, buffer_fill);
        }
        maybe_adjust_drift_control(now);
        if (now >= next_snapshot_at_) {
            RealtimeAudioSnapshot snapshot;
            snapshot.steady_time_ns = steady_now_ns;
            snapshot.input_rms = input_rms;
            snapshot.input_peak = input_peak;
            snapshot.buffer_fill_ratio = buffer_fill;
            snapshot.rendered_blocks = tracker_.current_rendered_blocks();
            snapshot.lost_blocks = tracker_.current_lost_blocks();
            snapshot.gap_blocks = tracker_.current_gap_blocks();
            snapshot.drift_ppm = tracker_.current_drift_ppm_estimate();
            snapshot.codec = active_codec_;
            snapshot.waveform_points = static_cast<int>(snapshot.waveform.size());
            snapshot.received_waveform_points = static_cast<int>(snapshot.received_waveform.size());
            if (options_.channels > 0 && options_.frame_samples > 0) {
                for (int i = 0; i < snapshot.waveform_points; ++i) {
                    const int src_frame = (i * options_.frame_samples) / snapshot.waveform_points;
                    const int safe_frame = std::clamp(src_frame, 0, options_.frame_samples - 1);
                    snapshot.waveform[static_cast<std::size_t>(i)] =
                        source_channels_[0][static_cast<std::size_t>(safe_frame)];
                }
                for (int i = 0; i < snapshot.received_waveform_points; ++i) {
                    const int src_frame = (i * options_.frame_samples) / snapshot.received_waveform_points;
                    const int safe_frame = std::clamp(src_frame, 0, options_.frame_samples - 1);
                    snapshot.received_waveform[static_cast<std::size_t>(i)] =
                        sink_channels_[0][static_cast<std::size_t>(safe_frame)];
                }
            }
            {
                std::lock_guard lock(snapshot_mutex_);
                latest_snapshot_ = std::move(snapshot);
                has_snapshot_ = true;
            }
            next_snapshot_at_ = now + kUiSnapshotInterval;
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

std::optional<AudioRuntime::RealtimeAudioSnapshot> AudioRuntime::latest_snapshot() const {
    std::lock_guard lock(snapshot_mutex_);
    if (!has_snapshot_) {
        return std::nullopt;
    }
    return latest_snapshot_;
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

bool AudioRuntime::set_source_codec(const std::string& codec) {
    if (!source_) {
        return false;
    }
    if (use_opus_codec(codec)) {
        aoo_format_opus format {};
        format.header.codec = AOO_CODEC_OPUS;
        format.header.nchannels = options_.channels;
        format.header.samplerate = options_.sample_rate_hz;
        format.header.blocksize = options_.frame_samples;
        format.bitrate = 0;
        format.complexity = 0;
        format.signal_type = OPUS_SIGNAL_MUSIC;
        format.application_type = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
        if (aoo_source_set_format(source_, &format.header) <= 0) {
            return false;
        }
        if (sink_) {
            aoo_sink_set_sourceoption(sink_, &reply_context_, options_.source_id, aoo_opt_format, &format, sizeof(aoo_format_opus));
        }
        active_codec_ = "opus-lowdelay";
    } else {
        aoo_format_pcm format {};
        format.header.codec = AOO_CODEC_PCM;
        format.header.nchannels = options_.channels;
        format.header.samplerate = options_.sample_rate_hz;
        format.header.blocksize = options_.frame_samples;
        format.bitdepth = AOO_PCM_FLOAT32;
        if (aoo_source_set_format(source_, &format.header) <= 0) {
            return false;
        }
        if (sink_) {
            aoo_sink_set_sourceoption(sink_, &reply_context_, options_.source_id, aoo_opt_format, &format, sizeof(aoo_format_pcm));
        }
        active_codec_ = "pcm";
    }
    sync_tracker_options();
    return true;
}

void AudioRuntime::sync_tracker_options() {
    AudioOptions tracker_options = options_;
    tracker_options.codec = active_codec_;
    tracker_.set_options(tracker_options);
}

void AudioRuntime::maybe_switch_runtime_codec(std::chrono::steady_clock::time_point now, float buffer_fill) {
    if (!source_ || now < next_codec_eval_at_) {
        return;
    }
    // Keep codec fixed unless Opus was explicitly selected by configuration/profile.
    // This avoids local runs unexpectedly switching to Opus on builds where decode
    // support may be unavailable at runtime.
    if (!use_opus_codec(options_.codec)) {
        return;
    }
    next_codec_eval_at_ = now + std::chrono::milliseconds(1000);

    const std::uint32_t current_lost = tracker_.current_lost_blocks();
    const std::uint32_t current_gap = tracker_.current_gap_blocks();
    const std::uint32_t lost_delta = current_lost - last_codec_eval_lost_blocks_;
    const std::uint32_t gap_delta = current_gap - last_codec_eval_gap_blocks_;
    last_codec_eval_lost_blocks_ = current_lost;
    last_codec_eval_gap_blocks_ = current_gap;
    if (tracker_.current_rendered_blocks() <= 100) {
        return;
    }

    const bool unstable = (lost_delta + gap_delta) >= 2 || buffer_fill < 0.18f;
    if (unstable) {
        codec_stable_since_ = now;
        if (active_codec_ == "pcm") {
            if (set_source_codec("opus-lowdelay")) {
                PJ_LOG(Info, "codec_switch from=pcm to=opus-lowdelay");
            }
        }
        return;
    }

    if (active_codec_ == "opus-lowdelay" && buffer_fill > 0.40f) {
        const auto stable_for = now - codec_stable_since_;
        if (stable_for >= std::chrono::seconds(8)) {
            if (set_source_codec("pcm")) {
                PJ_LOG(Info, "codec_switch from=opus-lowdelay to=pcm");
            }
        }
    }
}

void AudioRuntime::maybe_adjust_drift_control(std::chrono::steady_clock::time_point now) {
    if (!sink_ || now < next_drift_adjust_at_) {
        return;
    }
    next_drift_adjust_at_ = now + std::chrono::milliseconds(500);

    const double drift_ppm = tracker_.current_drift_ppm_estimate();
    const float bandwidth = drift_to_timefilter_bandwidth(drift_ppm);
    // AOO API uses the misspelled symbol name "...bandwith".
    aoo_sink_set_timefilter_bandwith(sink_, bandwidth);
}

void AudioRuntime::maybe_adjust_sink_buffer(float buffer_fill, std::chrono::steady_clock::time_point now) {
    if (!sink_ || now < next_buffer_adjust_at_) {
        return;
    }

    constexpr int kMinSinkBufferMs = 5;
    constexpr int kIncreaseStepMs = 1;
    constexpr int kDecreaseStepMs = 1;
    constexpr float kLowFillThreshold = 0.22f;
    constexpr float kHighFillThreshold = 0.70f;
    constexpr int kIncreaseStreak = 3;
    constexpr int kDecreaseStreak = 24;

    if (buffer_fill < kLowFillThreshold) {
        ++low_fill_streak_;
        high_fill_streak_ = 0;
    } else if (buffer_fill > kHighFillThreshold) {
        ++high_fill_streak_;
        low_fill_streak_ = 0;
    } else {
        low_fill_streak_ = 0;
        high_fill_streak_ = 0;
    }

    int next_buffer_ms = current_sink_buffer_ms_;
    if (low_fill_streak_ >= kIncreaseStreak) {
        next_buffer_ms = std::min(max_sink_buffer_ms_, current_sink_buffer_ms_ + kIncreaseStepMs);
        low_fill_streak_ = 0;
    } else if (high_fill_streak_ >= kDecreaseStreak) {
        next_buffer_ms = std::max(kMinSinkBufferMs, current_sink_buffer_ms_ - kDecreaseStepMs);
        high_fill_streak_ = 0;
    }

    if (next_buffer_ms != current_sink_buffer_ms_) {
        aoo_sink_set_buffersize(sink_, next_buffer_ms);
        current_sink_buffer_ms_ = next_buffer_ms;
        options_.sink_buffer_ms = next_buffer_ms;
        sync_tracker_options();
        next_buffer_adjust_at_ = now + std::chrono::milliseconds(500);
        return;
    }
    next_buffer_adjust_at_ = now + std::chrono::milliseconds(80);
}

}  // namespace peerjam
