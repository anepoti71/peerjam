#pragma once

#include "peerjam/logic/audio/audio_stream.hpp"

#include <memory>

namespace peerjam {

class AudioPlaybackSink {
  public:
    virtual ~AudioPlaybackSink() = default;
    virtual bool start(const AudioOptions& options) = 0;
    virtual bool write_interleaved(const float* input, int frames, int channels) = 0;
    virtual void stop() = 0;
};

std::unique_ptr<AudioPlaybackSink> make_audio_playback_sink(const AudioOptions& options);

}  // namespace peerjam
