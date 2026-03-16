#include "peerjam/logic/audio/capture/audio_capture_source.hpp"
#include "peerjam/logic/audio/portaudio_runtime.hpp"

#include "peerjam/common/logger.hpp"

#if defined(PEERJAM_HAS_PORTAUDIO)
#include <portaudio.h>
#endif

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace peerjam {
namespace {

class SyntheticToneCaptureSource final : public AudioCaptureSource {
  public:
    bool start(const AudioOptions& options) override {
        sample_rate_hz_ = options.sample_rate_hz;
        tone_hz_ = options.tone_hz;
        tone_level_ = options.tone_level;
        phase_ = 0.0;
        return true;
    }

    bool read_interleaved(float* output, int frames, int channels) override {
        if (!output || frames <= 0 || channels <= 0 || sample_rate_hz_ <= 0) {
            return false;
        }
        const double pi = std::acos(-1.0);
        const double phase_inc = 2.0 * pi * tone_hz_ / static_cast<double>(sample_rate_hz_);
        for (int i = 0; i < frames; ++i) {
            const float sample = static_cast<float>(std::sin(phase_) * tone_level_);
            phase_ += phase_inc;
            if (phase_ >= 2.0 * pi) {
                phase_ -= 2.0 * pi;
            }
            for (int ch = 0; ch < channels; ++ch) {
                output[static_cast<std::size_t>(i * channels + ch)] = sample;
            }
        }
        return true;
    }

    void stop() override {}

  private:
    int sample_rate_hz_ {};
    double tone_hz_ {};
    double tone_level_ {};
    double phase_ {};
};

#if defined(PEERJAM_HAS_PORTAUDIO)
class PortAudioCaptureSource final : public AudioCaptureSource {
  public:
    bool start(const AudioOptions& options) override {
        options_ = options;
        if (!retain_portaudio()) {
            PJ_LOG(Error, "PortAudio capture init failed");
            return false;
        }
        pa_retained_ = true;
        if (options.input_device_index >= 0) {
            PaStreamParameters input_params {};
            input_params.device = options.input_device_index;
            const PaDeviceInfo* info = Pa_GetDeviceInfo(input_params.device);
            if (!info || info->maxInputChannels <= 0) {
                release_portaudio();
                pa_retained_ = false;
                stream_ = nullptr;
                return false;
            }
            input_params.channelCount = options.channels;
            input_params.sampleFormat = paFloat32;
            input_params.suggestedLatency = info->defaultLowInputLatency;
            input_params.hostApiSpecificStreamInfo = nullptr;
            const auto specific_err = Pa_OpenStream(
                &stream_,
                &input_params,
                nullptr,
                static_cast<double>(options.sample_rate_hz),
                options.frame_samples,
                paNoFlag,
                nullptr,
                nullptr
            );
            if (specific_err != paNoError || !stream_) {
                release_portaudio();
                pa_retained_ = false;
                stream_ = nullptr;
                return false;
            }
        } else {
            const auto err = Pa_OpenDefaultStream(
                &stream_,
                options.channels,
                0,
                paFloat32,
                static_cast<double>(options.sample_rate_hz),
                options.frame_samples,
                nullptr,
                nullptr
            );
            if (err != paNoError || !stream_) {
                release_portaudio();
                pa_retained_ = false;
                stream_ = nullptr;
                return false;
            }
        }
        if (Pa_StartStream(stream_) != paNoError) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
            release_portaudio();
            pa_retained_ = false;
            return false;
        }
        return true;
    }

    bool read_interleaved(float* output, int frames, int channels) override {
        if (!stream_ || !output || frames <= 0 || channels <= 0 || channels != options_.channels) {
            return false;
        }
        const long readable = Pa_GetStreamReadAvailable(stream_);
        if (readable < 0) {
            return false;
        }
        if (readable < static_cast<long>(frames)) {
            std::fill(output, output + static_cast<std::size_t>(frames * channels), 0.0f);
            return true;
        }
        return Pa_ReadStream(stream_, output, frames) == paNoError;
    }

    void stop() override {
        if (stream_) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
        if (pa_retained_) {
            release_portaudio();
            pa_retained_ = false;
        }
    }

  private:
    AudioOptions options_;
    PaStream* stream_ {nullptr};
    bool pa_retained_ {false};
};
#endif

}  // namespace

std::unique_ptr<AudioCaptureSource> make_audio_capture_source(const AudioOptions& options) {
#if defined(PEERJAM_HAS_PORTAUDIO)
    if (options.device_io_enabled) {
        return std::make_unique<PortAudioCaptureSource>();
    }
#else
    if (options.device_io_enabled) {
        PJ_LOG(Info, "audio capture requested but PortAudio backend is not compiled; falling back to synthetic capture");
    }
#endif
    (void)options;
    return std::make_unique<SyntheticToneCaptureSource>();
}

std::string list_audio_io_devices_json() {
#if !defined(PEERJAM_HAS_PORTAUDIO)
    return "[]";
#else
    if (!retain_portaudio()) {
        PJ_LOG(Error, "PortAudio device listing init failed");
        return "[]";
    }
    const int count = Pa_GetDeviceCount();
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            continue;
        }
        if (!first) {
            os << ",";
        }
        first = false;
        std::string name = info->name ? info->name : "";
        std::replace(name.begin(), name.end(), '"', '\'');
        os << "{"
           << "\"index\":" << i << ","
           << "\"name\":\"" << name << "\","
           << "\"max_input_channels\":" << info->maxInputChannels << ","
           << "\"max_output_channels\":" << info->maxOutputChannels
           << "}";
    }
    os << "]";
    release_portaudio();
    return os.str();
#endif
}

}  // namespace peerjam
