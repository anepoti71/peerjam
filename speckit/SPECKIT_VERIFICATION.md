# PeerJam Native — Speckit Verification Report

## Scope

This document verifies the current codebase (`src/` and `include/`) against the thirteen Non-Negotiable Principles and the Required Acceptance Criteria in `speckit.constitution`.

Status codes:
- **PASS** — fully satisfied by existing code
- **PARTIAL** — partially satisfied; specific gaps noted
- **FAIL** — principle is not satisfied; action required

---

## Principle Verification

### 1. Music First

**Status: PARTIAL**

The scoring model (`path_scorer.cpp`) correctly penalizes jitter with a 2× multiplier and uses p95 percentiles rather than mean latency, which is appropriate for timing-sensitive decisions. The recommendation engine (`measurement.cpp`) classifies paths as ready/marginal/not-ready with codec and buffer recommendations.

Gap: The recommendation thresholds (latency_p95 ≤ 12 ms for aggressive ready) are defined as constants in implementation code, not as named policy values that can be reviewed or adjusted without rebuilding. Timing-regression tests do not exist.

Required action: Extract recommendation thresholds to named constants in a header or policy struct. Define at least one integration test that asserts a path regression is classified as a failure.

---

### 2. Performance Is Mandatory

**Status: PARTIAL**

The hot receive path (`aoo_peer_client.cpp`, receiver loop) uses `poll()` and non-blocking `recv`, avoiding blocking syscalls. The UDP socket wrapper (`udp_datagram_socket.cpp`) avoids unnecessary copies by writing directly into caller-provided buffers. The probe and path-scoring paths are structurally outside the media processing path.

Gap: `PathSelectionReport::to_json()` (`candidate.cpp`) uses `std::string` concatenation inside what could become a polling loop if telemetry is exported continuously. The audio runtime (`audio_runtime.cpp`) allocates `std::vector<float>` inside `run()` on each iteration via `audio_in` and `audio_out` locals — this is in the hot path.

Required action: Move `audio_in`/`audio_out` buffers to pre-allocated members. Audit all `std::string` construction in timer-sensitive callbacks.

---

### 3. Direct Media Path by Default

**Status: PARTIAL**

The constitution is structurally respected: signaling (AOO network client) is used only for peer discovery. The `CandidateGatherer` collects host and server-reflexive candidates; relay candidates are de-prioritized in scoring.

Gap: Path switching is measured and classified (`path_manager.cpp`, `reprobe_and_maybe_switch()`) but the selected path is never actively applied to rebind the UDP socket or redirect AOO media traffic. The peer client always uses the initial signaling-provided address. The selected path is measurement-only, not operationally enforced.

Required action: Implement media-path rebinding after path manager election. This is item 2 in `LAYER_SPECIFICATION.md` next steps.

---

### 4. Public-Internet Reality

**Status: PASS**

The system uses p95 percentiles for latency and jitter, which correctly accounts for tail behavior rather than average behavior. Stability metrics are tracked across a moving window. The scoring model includes a loss penalty that scales aggressively. The readiness classifier uses conservative thresholds appropriate for internet paths (not LAN-only).

All UDP operations use non-blocking sockets with proper error handling. The probe engine uses configurable warmup rounds to account for route settling.

No gaps identified. The system correctly frames all latency in terms of one-way estimates derived from RTT/2.

---

### 5. Small Buffers, Controlled Adaptation

**Status: PARTIAL**

The recommendation engine specifies initial jitter-buffer targets (5 ms aggressive, 5–8 ms moderate, 7.5–12 ms marginal) and the AOO sink is configured with a `sink_buffer_ms` option passed in from `PeerOptions`.

Gap: There is no adaptive buffer controller. The buffer is set at session start and never adjusted at runtime based on ongoing jitter measurements. Buffer fill, underrun, and overrun counts are tracked at the audio stream level but are not fed back to a controller that expands or contracts the buffer target.

Required action: Implement a buffer adaptation controller that reads `AudioStreamReport::buffer_fill_ratio` and adjusts `sink_buffer_ms` via AOO API within the bounds defined in the layer specification (5–20 ms), with slow decrease and threshold-gated increase.

---

### 6. Continuous Synchronization

**Status: PARTIAL**

`AudioStreamTracker` (`audio_stream.cpp`) performs continuous monitoring of buffer fill ratio and estimates drift via linear regression over time. The probe runtime monitors path metrics continuously in the background reprobe thread.

Gap: There is no clock offset tracker between peers. Drift is estimated only from buffer fill trend, which is an indirect proxy. No direct timestamp-delta comparison between sender and receiver is performed. Long-session stability is untested.

Required action: Implement a clock offset estimator that compares sender media timestamps against local receive time across packets. Feed this into the drift correction layer.

---

### 7. Gentle Drift Correction

**Status: PARTIAL**

The `AudioStreamReport` exposes a `drift_estimate_ppm` derived from buffer fill regression. AOO provides internal resampling (`dynamic_resampling` flag set in `audio_runtime.cpp`).

Gap: The drift estimate is computed but never used to control AOO's resampling rate. The correction feedback loop is incomplete. Emergency recovery actions (sample drop, silence insertion) are not defined. The near-unity rate range (`0.9999–1.0001`) is not enforced or validated.

Required action: Wire `drift_estimate_ppm` into AOO's `sink_resample_threshold` or an equivalent rate-control call. Define an emergency recovery path with logging when drift correction is insufficient.

---

### 8. Measurable State

**Status: PASS**

The system exposes a comprehensive set of metrics:
- Path layer: RTT, one-way estimate, jitter, loss, reorder, stability score, candidate type, switch events (`PathSelectionReport`)
- Probe layer: per-packet timing, percentile summary stats, readiness classification, codec recommendation (`MeasurementReport`)
- Audio layer: block counts, RMS/peak, buffer fill ratio, drift estimate, event counts for loss/reorder/resend/gap (`AudioStreamReport`)
- All reports serialize to JSON for external consumption

One minor gap: audio events (loss, reorder, resend, gap counts) are tracked but not included in the JSON output of `AudioStreamReport::to_json()`. The main report in `aoo_peer_client.cpp` does include them, but the canonical report struct should serialize them.

---

### 9. Explicit Timing Authority

**Status: FAIL**

There is no master clock election mechanism. The session does not assign a timing authority role. No peer is designated master or follower. The timing authority is implicitly the sender (AOO source), but this is not explicit, not exposed in session state, and not transferable.

Required action: This is item 4 in `LAYER_SPECIFICATION.md` next steps. Implement master clock election during session setup. Expose the elected role in the session report. Define a re-election procedure for when the master peer disconnects.

---

### 10. PCM First, Codec Optional

**Status: PARTIAL**

The default audio configuration uses PCM float32 (`audio_runtime.cpp`). The recommendation engine correctly suggests Opus only for degraded paths. The scoring model penalizes conditions that would require Opus.

Gap: Codec switching is recommendation-only. There is no mechanism to actually switch from PCM to Opus at runtime based on recommendations. The `AooPeerClient` reads the recommendation but does not act on it for codec selection. Opus is a compile-time dependency but never instantiated in practice.

Required action: Implement runtime codec negotiation. When the path manager recommends Opus, `AooPeerClient` must reconfigure the AOO source format and signal the change to the remote peer via the control channel.

---

### 11. Layer Separation

**Status: PASS**

The layer boundaries are well-defined and respected:
- `UdpDatagramSocket` owns raw UDP I/O only
- `ProbeRuntime` owns probe packet dispatch; does not make path decisions
- `PathManager` owns path selection; does not touch audio state
- `AudioRuntime` owns AOO lifecycle; receives only what it needs via datagram routing
- `AooPeerClient` is the orchestrator and the only component that crosses layer boundaries

Cross-layer coupling is explicit: `AooPeerClient` passes references downward and reads reports upward. No layer reads another layer's internal state directly.

---

### 12. Geographic Truthfulness

**Status: PARTIAL**

The readiness classifier includes a latency threshold (not-ready when p95 > 30 ms) which is consistent with geographic awareness. The `MeasurementReport` documents that the 30 ms threshold is geography-constrained.

Gap: The system does not explicitly distinguish geography-imposed latency from transport-imposed latency in its output. The report says "not ready" but does not explain whether the cause is path quality or physical distance. No geographic latency budget is reported.

Required action: When `latency_p95 > 30 ms`, include a note in the report distinguishing whether the latency is consistent with geographic distance (stable, low jitter, high loss = unlikely; stable, low jitter, low loss = geographic) versus network instability. This is guidance, not a hard requirement.

---

### 13. Fail Safe, Not Fake

**Status: PASS**

The readiness classifier (`measurement.cpp`) produces explicit ready/marginal/not-ready states. The JSON report includes the classification string. The thresholds are conservative (not optimistic). Buffer size recommendations increase explicitly with path quality degradation rather than hiding it.

The system does not inflate reported quality. When path data is insufficient or loss is high, the classifier returns `not_ready` and recommends against PCM streaming.

---

## Required Acceptance Criteria Checklist

| Criterion | Status | Evidence |
|-----------|--------|----------|
| P2P UDP media is the primary path | PARTIAL | Structurally yes; path selection not applied to actual routing |
| Performance-sensitive paths are allocation-conscious | PARTIAL | Receiver loop is non-blocking; audio run loop allocates vectors per call |
| Runtime metrics available for path, transport, sync | PASS | PathSelectionReport, MeasurementReport, AudioStreamReport |
| Clock drift managed continuously with smooth correction | PARTIAL | Drift estimated; correction loop not wired |
| Buffering intentionally small and measurable | PARTIAL | Initial sizing correct; no adaptive controller |
| Architecture viable on real public-internet links | PASS | Scoring model, thresholds, and NAT traversal are internet-realistic |

---

## Summary

| Layer | Status |
|-------|--------|
| Session and control | PARTIAL — no state machine, no timing authority election |
| Candidate discovery and NAT | PARTIAL — no full candidate set exchange via signaling |
| Path probing and selection | PARTIAL — selection measured but not applied to routing |
| Media transport | PARTIAL — PCM only; Opus fallback not activated |
| Audio framing and codec | PARTIAL — codec switching is recommendation-only |
| Jitter buffer | PARTIAL — initial sizing only; no adaptive controller |
| Clock synchronization | PARTIAL — buffer fill proxy only; no offset tracker |
| Drift detection and correction | PARTIAL — drift estimated; correction not wired |
| Audio device | PARTIAL — synthetic tone only; no real device I/O |
| Monitoring and policy | PASS — comprehensive metrics and classification |

## Priority Order for Closing Gaps

1. Apply path selection to actual media routing (Principle 3, acceptance criterion 1)
2. Wire drift estimate into AOO resampling control (Principle 7, acceptance criterion 4)
3. Implement adaptive jitter buffer controller (Principle 5, acceptance criterion 5)
4. Add master clock election (Principle 9)
5. Implement codec switching from recommendations (Principle 10)
6. Replace synthetic audio with real device I/O (Layer 9)
7. Exchange full candidate sets via signaling (Layer 2)
