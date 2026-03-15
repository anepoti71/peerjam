#pragma once

#include "peerjam/logic/network/candidate.hpp"
#include "peerjam/logic/probe/measurement.hpp"

namespace peerjam {

class PathScorer {
  public:
    PathScore score(const MeasurementReport& report, const NetworkCandidate& local, const NetworkCandidate& remote) const;
};

}  // namespace peerjam
