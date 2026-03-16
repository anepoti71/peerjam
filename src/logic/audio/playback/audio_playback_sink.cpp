#include "peerjam/logic/audio/playback/audio_playback_sink.hpp"

#include "peerjam/common/logger.hpp"

#if defined(PEERJAM_HAS_PORTAUDIO)
#include <portaudio.h>
#endif

namespace peerjam {
namespace {

class NullPlaybackSink final : public AudioPlaybackSink {
  public:
    bool start(const AudioOptions&) override { return true; }
    bool write_interleaved(const float*, int, int) override { return true; }
    void stop() override {}
};

#if defined(PEERJAM_HAS_PORTAUDIO)
class PortAudioPlaybackSink final : public AudioPlaybackSink {
  public:
    bool start(const AudioOptions& options) override {
        options_ = options;
        if (Pa_Initialize() != paNoError) {
            return false;
        }

        PaStreamParameters output_params {};
        output_params.device = options.output_device_index >= 0
            ? options.output_device_index
            : Pa_GetDefaultOutputDevice();
        if (output_params.device == paNoDevice) {
            Pa_Terminate();
            return false;
        }
        const PaDeviceInfo* info = Pa_GetDeviceInfo(output_params.device);
        if (!info || info->maxOutputChannels <= 0) {
            Pa_Terminate();
            return false;
        }
        output_params.channelCount = options.channels;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = info->defaultLowOutputLatency;
        output_params.hostApiSpecificStreamInfo = nullptr;

        const auto err = Pa_OpenStream(
            &stream_,
            nullptr,
            &output_params,
            static_cast<double>(options.sample_rate_hz),
            options.frame_samples,
            paNoFlag,
            nullptr,
            nullptr
        );
        if (err != paNoError || !stream_) {
            Pa_Terminate();
            stream_ = nullptr;
            return false;
        }
        if (Pa_StartStream(stream_) != paNoError) {
            Pa_CloseStream(stream_);
            stream_ = nullptr;
            Pa_Terminate();
            return false;
        }
        return true;
    }

    bool write_interleaved(const float* input, int frames, int channels) override {
        if (!stream_ || !input || frames <= 0 || channels != options_.channels) {
            return false;
        }
        const auto err = Pa_WriteStream(stream_, input, frames);
        return err == paNoError || err == paOutputUnderflowed;
    }

    void stop() override {
        if (stream_) {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
        Pa_Terminate();
    }

  private:
    AudioOptions options_;
    PaStream* stream_ {nullptr};
};
#endif

}  // namespace

std::unique_ptr<AudioPlaybackSink> make_audio_playback_sink(const AudioOptions& options) {
#if defined(PEERJAM_HAS_PORTAUDIO)
    if (options.device_io_enabled) {
        return std::make_unique<PortAudioPlaybackSink>();
    }
#else
    if (options.device_io_enabled) {
        PJ_LOG(Info, "audio playback requested but PortAudio backend is not compiled; playback disabled");
    }
#endif
    (void)options;
    return std::make_unique<NullPlaybackSink>();
}

}  // namespace peerjam
