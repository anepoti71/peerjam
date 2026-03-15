#pragma once

#include "peerjam/logic/audio/audio_stream.hpp"
#include "peerjam/logic/probe/measurement.hpp"

#include <optional>
#include <string>

namespace peerjam {

bool write_session_profile(const std::string& path, const Recommendation& recommendation);
std::optional<Recommendation> read_session_profile(const std::string& path);
void apply_session_profile(const Recommendation& recommendation, AudioOptions& options);

}  // namespace peerjam
