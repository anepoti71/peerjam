#pragma once

namespace peerjam {

struct RecommendationPolicyOptions {
    int sample_rate_hz {48000};
    int aggressive_frame_samples {64};
    int conservative_frame_samples {128};
    int fallback_frame_samples {128};
    double not_ready_loss_percent {1.0};
    double not_ready_one_way_p95_ms {30.0};
    double not_ready_jitter_p95_ms {5.0};
    double aggressive_ready_loss_percent {0.1};
    double aggressive_ready_one_way_p95_ms {12.0};
    double aggressive_ready_jitter_p95_ms {1.0};
    double conservative_ready_loss_percent {0.5};
    double conservative_ready_one_way_p95_ms {18.0};
    double conservative_ready_jitter_p95_ms {2.5};
    double not_ready_buffer_min_ms {8.0};
    double not_ready_buffer_max_ms {20.0};
    double ready_buffer_min_ms {5.0};
    double ready_buffer_max_ms {8.0};
    double marginal_buffer_min_ms {7.5};
    double marginal_buffer_max_ms {12.0};
};

}  // namespace peerjam
