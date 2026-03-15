#include "peerjam/app/session_profile.hpp"

#include "peerjam/common/json_utils.hpp"
#include "peerjam/common/logger.hpp"
#include "peerjam/logic/audio/audio_stream.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

namespace peerjam {
namespace {

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

bool line_has_key(std::string_view line, const std::string& key) {
    return trim(line).rfind(json_quote(key) + ":", 0) == 0;
}

std::optional<std::string> parse_string_value(std::string_view line) {
    auto fail = [line](const char* reason) -> std::optional<std::string> {
        PJ_LOG(Info, "session profile parse error: " << reason << " line=" << std::string(line));
        return std::nullopt;
    };
    const auto colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos) {
        return fail("missing colon");
    }
    const auto first_quote = line.find('"', colon_pos + 1);
    if (first_quote == std::string_view::npos) {
        return fail("missing opening quote");
    }
    std::string value;
    bool escaped = false;
    for (std::size_t i = first_quote + 1; i < line.size(); ++i) {
        const char ch = line[i];
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                return value;
            }
            value.push_back(ch);
            continue;
        }

        escaped = false;
        switch (ch) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '/':
                value.push_back('/');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                if (i + 4 >= line.size()) {
                    return fail("truncated unicode escape");
                }
                auto hex_value = [](char c) -> int {
                    if (c >= '0' && c <= '9') {
                        return c - '0';
                    }
                    if (c >= 'a' && c <= 'f') {
                        return 10 + (c - 'a');
                    }
                    if (c >= 'A' && c <= 'F') {
                        return 10 + (c - 'A');
                    }
                    return -1;
                };
                const int h1 = hex_value(line[i + 1]);
                const int h2 = hex_value(line[i + 2]);
                const int h3 = hex_value(line[i + 3]);
                const int h4 = hex_value(line[i + 4]);
                if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
                    return fail("invalid unicode escape");
                }
                const int codepoint = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                if (codepoint <= 0x7F) {
                    value.push_back(static_cast<char>(codepoint));
                } else {
                    return fail("non-ascii unicode escape unsupported");
                }
                i += 4;
                break;
            }
            default:
                return fail("invalid escape sequence");
        }
    }
    if (escaped) {
        return fail("trailing escape sequence");
    }
    return fail("unterminated string");
}

template <typename T>
std::optional<T> parse_number_value(std::string_view line) {
    const auto colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto raw = trim(line.substr(colon_pos + 1));
    if (!raw.empty() && raw.back() == ',') {
        raw.pop_back();
    }
    std::istringstream is(raw);
    T value {};
    if (!(is >> value)) {
        return std::nullopt;
    }
    return value;
}

}  // namespace
bool write_session_profile(const std::string& path, const Recommendation& recommendation) {
    if (path.empty()) {
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << recommendation.to_json() << "\n";
    return static_cast<bool>(out);
}

std::optional<Recommendation> read_session_profile(const std::string& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    Recommendation recommendation;
    std::optional<std::string> status;
    std::optional<std::string> codec;
    std::optional<std::string> rationale;
    std::optional<int> sample_rate_hz;
    std::optional<int> frame_samples;
    std::optional<double> initial_buffer_ms;

    std::string line;
    while (std::getline(in, line)) {
        if (line_has_key(line, "status")) {
            status = parse_string_value(line);
        } else if (line_has_key(line, "codec")) {
            codec = parse_string_value(line);
        } else if (line_has_key(line, "sample_rate_hz")) {
            sample_rate_hz = parse_number_value<int>(line);
        } else if (line_has_key(line, "frame_samples")) {
            frame_samples = parse_number_value<int>(line);
        } else if (line_has_key(line, "initial_buffer_ms")) {
            initial_buffer_ms = parse_number_value<double>(line);
        } else if (line_has_key(line, "rationale")) {
            rationale = parse_string_value(line);
        }
    }

    if (!status || !codec || !rationale || !sample_rate_hz || !frame_samples || !initial_buffer_ms) {
        return std::nullopt;
    }

    recommendation.status = *status;
    recommendation.codec = *codec;
    recommendation.rationale = *rationale;
    recommendation.sample_rate_hz = *sample_rate_hz;
    recommendation.frame_samples = *frame_samples;
    recommendation.initial_buffer_ms = *initial_buffer_ms;
    return recommendation;
}

void apply_session_profile(const Recommendation& recommendation, AudioOptions& options) {
    options.sample_rate_hz = recommendation.sample_rate_hz;
    options.frame_samples = recommendation.frame_samples;
    options.sink_buffer_ms = static_cast<int>(recommendation.initial_buffer_ms + 0.5);
    options.codec = recommendation.codec == "opus-lowdelay" ? "opus-lowdelay" : "pcm";

    // Keep the source side slightly tighter while respecting the tested operating point.
    const int source_buffer_ms = static_cast<int>((recommendation.initial_buffer_ms * 0.6) + 0.5);
    options.source_buffer_ms = source_buffer_ms > 1 ? source_buffer_ms : 1;
}

}  // namespace peerjam
