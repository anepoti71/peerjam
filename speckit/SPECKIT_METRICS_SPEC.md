# PeerJam Native — Metrics Specification

## Scope

This document defines the required observable metrics for each layer, per Principle 8 (Measurable State) of `speckit.constitution`. Each metric entry specifies the name, type, unit, source, and the current implementation status.

---

## Layer 3: Path Probing and Selection

### Required by constitution

All metrics defined. Sources: `MeasurementReport`, `PathSelectionReport`.

| Metric | Type | Unit | Source struct | Status |
|--------|------|------|---------------|--------|
| `rtt_p50_ms` | float | ms | MeasurementReport | IMPL |
| `rtt_p95_ms` | float | ms | MeasurementReport | IMPL |
| `rtt_p99_ms` | float | ms | MeasurementReport | IMPL |
| `one_way_latency_ms` | float | ms | MeasurementReport (rtt/2) | IMPL |
| `jitter_p50_ms` | float | ms | MeasurementReport | IMPL |
| `jitter_p95_ms` | float | ms | MeasurementReport | IMPL |
| `loss_percent` | float | % | MeasurementReport | IMPL |
| `reorder_count` | uint32 | count | MeasurementReport | IMPL |
| `path_score` | float | dimensionless | PathScore | IMPL |
| `candidate_type` | enum | Host/SRflx/Relay | NetworkCandidate | IMPL |
| `switch_event_count` | uint32 | count | PathSelectionReport | IMPL |
| `readiness` | enum | ready/marginal/not_ready | Recommendation | IMPL |

---

## Layer 6: Jitter Buffer

### Required by constitution (Layer 6 spec)

| Metric | Type | Unit | Source struct | Status |
|--------|------|------|---------------|--------|
| `buffer_fill_ratio` | float | 0.0–1.0 | AudioStreamReport | IMPL |
| `underrun_count` | uint32 | count | AudioStreamReport (inferred from AOO events) | PARTIAL |
| `overrun_count` | uint32 | count | AudioStreamReport | MISSING |
| `reorder_count` | uint32 | count | AudioStreamReport | IMPL |
| `missing_frame_count` | uint32 | count | AudioStreamReport (xrun_count proxy) | PARTIAL |

Notes:
- `underrun_count` and `overrun_count` are inferred from AOO event callbacks (block loss, gap events) but are not separated by type in the current `AudioStreamReport`. They must be split.
- `missing_frame_count` is tracked as `xrun_count` in `AudioStreamReport`; rename to align with layer specification terminology.

---

## Layer 7/8: Clock Synchronization and Drift

### Required by constitution

| Metric | Type | Unit | Source struct | Status |
|--------|------|------|---------------|--------|
| `clock_offset_estimate_ms` | float | ms | (none) | MISSING |
| `effective_sample_rate_mismatch_ppm` | float | ppm | AudioStreamReport.drift_estimate_ppm | PARTIAL |
| `playout_offset_from_target_ms` | float | ms | (none) | MISSING |
| `drift_estimate_ppm` | float | ppm | AudioStreamReport | IMPL |
| `playback_rate_multiplier` | float | ratio | (none) | MISSING |
| `buffer_fill_trend` | float | ratio/s | (inferred from regression slope) | PARTIAL |
| `emergency_recovery_count` | uint32 | count | (none) | MISSING |

Notes:
- `clock_offset_estimate_ms` requires a peer timestamp comparison mechanism (not yet implemented).
- `playback_rate_multiplier` requires AOO to expose the current resampling ratio, or a local tracker.
- `emergency_recovery_count` requires defining what constitutes an emergency (e.g., hard reset, sample drop) and counting occurrences.

---

## Layer 5: Audio Framing

### Required by constitution

| Metric | Type | Unit | Source struct | Status |
|--------|------|------|---------------|--------|
| `codec_name` | string | PCM/Opus | AudioStreamReport | PARTIAL |
| `frame_size_samples` | uint32 | samples | (from PeerOptions) | PARTIAL |
| `codec_added_latency_ms` | float | ms | (none) | MISSING |

Notes:
- `codec_added_latency_ms` must be reported per codec: PCM = 0, Opus low-delay = algorithmic delay in ms.
- Frame size is configured at session start but not included in the runtime report.

---

## Layer 4: Media Transport

| Metric | Type | Unit | Source struct | Status |
|--------|------|------|---------------|--------|
| `packets_sent` | uint64 | count | (AOO internal) | MISSING |
| `packets_received` | uint64 | count | (AOO internal) | MISSING |
| `resend_requests` | uint32 | count | AudioStreamReport.resend_count | IMPL |
| `active_path_address` | string | IP:port | PathSelectionReport | IMPL |
| `active_path_candidate_type` | enum | Host/SRflx/Relay | PathSelectionReport | IMPL |

---

## Layer 9: Audio Device

| Metric | Type | Unit | Source | Status |
|--------|------|------|--------|--------|
| `device_capture_latency_ms` | float | ms | (none — synthetic only) | MISSING |
| `device_playback_latency_ms` | float | ms | (none — synthetic only) | MISSING |
| `callback_size_samples` | uint32 | samples | (none) | MISSING |

All device metrics are blocked by ADR-0002 (synthetic audio). They become required when real device I/O is integrated.

---

## Layer 10: Monitoring and Policy

The `Recommendation` struct produced by `MeasurementTracker::summarize()` must expose all of the following in the JSON output:

| Field | Current JSON key | Status |
|-------|-----------------|--------|
| ready/marginal/not_ready | `"status"` | IMPL |
| recommended codec | `"codec"` | IMPL |
| recommended frame size (samples) | `"frame_size"` | IMPL |
| recommended initial jitter buffer (ms) | `"buffer_ms_min"`, `"buffer_ms_max"` | IMPL |
| reason for not-ready (if applicable) | (none) | MISSING |
| master clock re-election required | (none) | MISSING |

---

## Metric Completeness Summary

| Layer | Required metrics | Implemented | Partial | Missing |
|-------|-----------------|-------------|---------|---------|
| Path probing | 12 | 12 | 0 | 0 |
| Jitter buffer | 5 | 2 | 2 | 1 |
| Clock sync/drift | 7 | 1 | 2 | 4 |
| Audio framing | 3 | 0 | 2 | 1 |
| Media transport | 5 | 3 | 0 | 2 |
| Audio device | 3 | 0 | 0 | 3 |
| Monitoring/policy | 6 | 4 | 0 | 2 |
