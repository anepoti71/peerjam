# PeerJam Native — Verification Report

## Scope

This document verifies the current codebase (`src/` and `include/`) against the thirteen
Non-Negotiable Principles and the Required Acceptance Criteria in
`.specify/memory/constitution.md`.

Status codes:

- **PASS** — fully satisfied by existing code
- **PARTIAL** — partially satisfied; specific gaps noted
- **FAIL** — principle is not satisfied; action required

---

## Principle Verification

### 1. Music First

**Status: PARTIAL**

The scoring model (`path_scorer.cpp`) correctly penalizes jitter with a 2× multiplier and
uses p95 percentiles rather than mean latency, which is appropriate for timing-sensitive
decisions. The recommendation engine (`measurement.cpp`) classifies paths as
ready/marginal/not-ready with codec and buffer recommendations.

Gap: The recommendation thresholds (latency_p95 ≤ 12 ms for aggressive ready) are defined
as named policy values in `recommendation_policy.hpp`, but timing-regression tests do not
exist.

Required action: Define at least one integration test that asserts a path regression is
classified as a failure.

---

### 2. Performance Is Mandatory

**Status: PARTIAL**

The hot receive path (`aoo_peer_client.cpp`, receiver loop) uses `poll()` and non-blocking
`recv`, avoiding blocking syscalls. The UDP socket wrapper (`udp_datagram_socket.cpp`) avoids
unnecessary copies by writing directly into caller-provided buffers. The probe and
path-scoring paths are structurally outside the media processing path.

Gap: `PathSelectionReport::to_json()` (`candidate.cpp`) uses `std::string` concatenation
inside what could become a polling loop if telemetry is exported continuously. The hot media
and audio loops themselves are allocation-conscious.

Realtime UI telemetry is now implemented as an optional out-of-band SSE publisher with
throttled updates (`ui.telemetry_interval_ms`, default 100 ms) so audio-block cadence and
sub-30 ms end-to-end latency budget are not gated by UI I/O.

Required action: Audit all `std::string` construction in timer-sensitive callbacks and avoid
continuous JSON serialization from high-frequency loops.

---

### 3. Direct Media Path by Default

**Status: PASS**

The constitution is structurally respected: signaling (AOO network client) is used only for
peer discovery. The `CandidateGatherer` collects host and server-reflexive candidates; relay
candidates are de-prioritized in scoring.

`AooPeerClient::apply_selected_path()` actively rebinds the UDP socket and atomically swaps
the active remote endpoint after initial selection and on accepted reprobe switches. This
makes the selected candidate pair operational, not report-only. The rebind introduces a brief
intentional receive gap while the socket is swapped; this is acceptable for path switching
and recoverable by AoO resend behavior.

---

### 4. Public-Internet Reality

**Status: PASS**

The system uses p95 percentiles for latency and jitter, which correctly accounts for tail
behavior rather than average behavior. Stability metrics are tracked across a moving window.
The scoring model includes a loss penalty that scales aggressively. The readiness classifier
uses conservative thresholds appropriate for internet paths (not LAN-only).

All UDP operations use non-blocking sockets with proper error handling. The probe engine uses
configurable warmup rounds to account for route settling.

No gaps identified. The system correctly frames all latency in terms of one-way estimates
derived from RTT/2.

---

### 5. Small Buffers, Controlled Adaptation

**Status: PASS**

`AudioRuntime::maybe_adjust_sink_buffer()` implements a threshold-gated adaptive jitter-buffer
controller with asymmetric behavior (fast guarded increase, slow decrease), and updates both
AOO sink buffer size and runtime tracker options.

The configured session/profile buffer target is now the runtime ceiling, preventing accidental
shrink below probe-calibrated safety margins due to a hardcoded limit.

---

### 6. Continuous Synchronization

**Status: PARTIAL**

`AudioStreamTracker` (`audio_stream.cpp`) performs continuous monitoring of buffer fill ratio
and estimates drift via linear regression over time. The probe runtime monitors path metrics
continuously in the background reprobe thread.

Gap: There is no clock offset tracker between peers. Drift is estimated only from buffer fill
trend, which is an indirect proxy. No direct timestamp-delta comparison between sender and
receiver is performed. Long-session stability is untested.

Required action: Implement a clock offset estimator that compares sender media timestamps
against local receive time across packets. Feed this into the drift correction layer.

---

### 7. Gentle Drift Correction

**Status: PASS**

Drift estimation is continuously sampled in `AudioStreamTracker`, and runtime correction is
wired through `AudioRuntime::maybe_adjust_drift_control()` by mapping the drift estimate into
AOO timefilter bandwidth (`aoo_sink_set_timefilter_bandwith`), with periodic low-rate updates.

---

### 8. Measurable State

**Status: PASS**

The system exposes a comprehensive set of metrics:

- Path layer: RTT, one-way estimate, jitter, loss, reorder, stability score, candidate type,
  switch events (`PathSelectionReport`)
- Probe layer: per-packet timing, percentile summary stats, readiness classification, codec
  recommendation (`MeasurementReport`)
- Audio layer: block counts, RMS/peak, buffer fill ratio, drift estimate, event counts for
  loss/reorder/resend/gap (`AudioStreamReport`)
- All reports serialize to JSON for external consumption
- Optional realtime UI telemetry exposes live captured waveform, received waveform, and
  one-way latency/jitter/bandwidth snapshots via a throttled local endpoint

No gaps identified. Audio events (loss, reorder, resend, gap counts) are serialized in
`AudioStreamReport::to_json()`.

---

### 9. Explicit Timing Authority

**Status: PASS**

`AooPeerClient` performs deterministic two-party timing authority election
(`elect_timing_authority`), exposes timing state in JSON reports, and resets timing role when
the control-plane connection drops.

Note: current behavior is explicitly based on a two-party session assumption.

---

### 10. PCM First, Codec Optional

**Status: PASS**

PCM remains default, with runtime codec adaptation enabled in
`AudioRuntime::maybe_switch_runtime_codec()`. Runtime transitions update both source format
and sink source option, and are logged for session diagnostics.

---

### 11. Layer Separation

**Status: PASS**

The layer boundaries are well-defined and respected:

- `UdpDatagramSocket` owns raw UDP I/O only
- `ProbeRuntime` owns probe packet dispatch; does not make path decisions
- `PathManager` owns path selection; does not touch audio state
- `AudioRuntime` owns AOO lifecycle; receives only what it needs via datagram routing
- `AooPeerClient` is the orchestrator and the only component that crosses layer boundaries

Cross-layer coupling is explicit: `AooPeerClient` passes references downward and reads
reports upward. No layer reads another layer's internal state directly.

---

### Build Portability

**Status: PASS**

Build configuration supports Linux and macOS dependency discovery without hardcoded
single-platform library paths, using package-managed discovery (`pkg-config`/standard CMake
finders) and explicit override for `AOO_ROOT`.

---

### 12. Geographic Truthfulness

**Status: PARTIAL**

The readiness classifier includes a latency threshold (not-ready when p95 > 30 ms) which is
consistent with geographic awareness. The `MeasurementReport` documents that the 30 ms
threshold is geography-constrained.

Gap: The system does not explicitly distinguish geography-imposed latency from
transport-imposed latency in its output. The report says "not ready" but does not explain
whether the cause is path quality or physical distance. No geographic latency budget is
reported.

Required action: When `latency_p95 > 30 ms`, include a note in the report distinguishing
whether the latency is consistent with geographic distance (stable, low jitter, low loss =
geographic) versus network instability. This is guidance, not a hard requirement.

---

### 13. Fail Safe, Not Fake

**Status: PASS**

The readiness classifier (`measurement.cpp`) produces explicit ready/marginal/not-ready
states. The JSON report includes the classification string. The thresholds are conservative
(not optimistic). Buffer size recommendations increase explicitly with path quality
degradation rather than hiding it.

The system does not inflate reported quality. When path data is insufficient or loss is high,
the classifier returns `not_ready` and recommends against PCM streaming.

---

## Required Acceptance Criteria Checklist

| Criterion | Status | Evidence |
|-----------|--------|----------|
| P2P UDP media is the primary path | PASS | Selected candidate pair is applied via socket rebind and remote endpoint swap |
| Performance-sensitive paths are allocation-conscious | PARTIAL | Receiver loop is non-blocking; audio run loop allocates vectors per call |
| Runtime metrics available for path, transport, sync | PASS | PathSelectionReport, MeasurementReport, AudioStreamReport |
| One-way latency budget guidance remains explicit (`< 30 ms`) | PASS | Readiness thresholds and realtime UI telemetry flag budget breach |
| Build portability across Linux/macOS | PASS | Dependency discovery uses package-manager compatible CMake finders and override paths |
| Clock drift managed continuously with smooth correction | PASS | Drift estimate drives AOO timefilter bandwidth updates |
| Buffering intentionally small and measurable | PASS | Runtime adaptive sink-buffer controller with bounded target |
| Architecture viable on real public-internet links | PASS | Scoring model, thresholds, and NAT traversal are internet-realistic |

---

## Summary

| Layer | Status |
|-------|--------|
| Session and control | PARTIAL — timing election is implemented; formal state machine remains |
| Candidate discovery and NAT | PARTIAL — full candidate set exchange via signaling still incomplete |
| Path probing and selection | PASS — selected path is applied to active routing |
| Media transport | PASS — direct path with runtime PCM/Opus operation |
| Audio framing and codec | PASS — runtime codec switching active |
| Jitter buffer | PASS — adaptive controller implemented |
| Clock synchronization | PARTIAL — buffer fill proxy only; no offset tracker |
| Drift detection and correction | PASS — drift estimate wired into AOO timefilter control |
| Audio device | PASS — selectable capture/playback devices with realtime waveform telemetry |
| Monitoring and policy | PASS — comprehensive metrics, classification, and realtime UI telemetry |

## Priority Order for Closing Gaps

1. Exchange full candidate sets via signaling (Layer 2)
2. Add explicit clock offset estimator beyond buffer-fill proxy (Layer 7)
3. Add timing-regression integration tests for readiness policy thresholds (Principle 1)
4. Add clearer geographic-latency attribution in session reports (Principle 12)
