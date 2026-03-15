#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace peerjam {

inline double percentile(const std::vector<double>& values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    auto ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const auto rank = static_cast<std::size_t>(std::ceil((p / 100.0) * ordered.size()));
    return ordered[std::min<std::size_t>(ordered.size() - 1, std::max<std::size_t>(rank, 1) - 1)];
}

}  // namespace peerjam
