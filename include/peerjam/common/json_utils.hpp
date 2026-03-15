#pragma once

#include <string>
#include <string_view>

namespace peerjam {

inline std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    constexpr char kHex[] = "0123456789ABCDEF";
    for (unsigned char ch : input) {
        switch (ch) {
            case '\"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[(ch >> 4) & 0x0F]);
                    out.push_back(kHex[ch & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

inline std::string json_quote(std::string_view input) {
    return "\"" + json_escape(input) + "\"";
}

}  // namespace peerjam
