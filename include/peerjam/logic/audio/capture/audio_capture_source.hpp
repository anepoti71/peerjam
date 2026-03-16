#pragma once

#include "peerjam/logic/audio/audio_stream.hpp"

#include <memory>
#include <string>

namespace peerjam {

class AudioCaptureSource {
  public:
    virtual ~AudioCaptureSource() = default;
    virtual bool start(const AudioOptions& options) = 0;
    virtual bool read_interleaved(float* output, int frames, int channels) = 0;
    virtual void stop() = 0;
};

std::unique_ptr<AudioCaptureSource> make_audio_capture_source(const AudioOptions& options);
std::string list_audio_io_devices_json();

}  // namespace peerjam
