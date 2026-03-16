#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace peerjam {

template <typename T>
inline double percentile(const std::vector<T>& values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    auto ordered = values;
    const auto rank = static_cast<std::size_t>(std::ceil((p / 100.0) * ordered.size()));
    const auto index = std::min<std::size_t>(ordered.size() - 1, std::max<std::size_t>(rank, 1) - 1);
    std::nth_element(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(index), ordered.end());
    return static_cast<double>(ordered[index]);
}

}  // namespace peerjam
