<!--
  Sync Impact Report
  ==================
  Version change: 1.0.1 → 1.0.2
  Source: Reconciled from speckit/speckit.constitution (now removed)

  Added sections:
  - Core Principles (13 principles, expanded from 5-slot template)
  - Required Acceptance Criteria
  - Change Policy
  - Governance

  Modified principles: N/A (initial fill)
  Removed sections: N/A

  Templates status:
  - .specify/templates/plan-template.md       — ✅ no changes needed
  - .specify/templates/spec-template.md       — ✅ no changes needed
  - .specify/templates/tasks-template.md      — ✅ no changes needed
  - .specify/templates/agent-file-template.md — ✅ no changes needed

  Deferred TODOs: none
-->

# PeerJam Native Constitution

## Purpose

PeerJam Native exists to enable real-time remote music collaboration over the public internet.
The system is not a general media-streaming platform. Every architectural decision MUST be
evaluated against the needs of live musical interaction: low one-way latency, stable rhythm,
predictable synchronization, and graceful degradation under adverse network conditions.

## Core Principles

### I. Music First

The primary optimization target is interactive musical performance, not bulk throughput,
archival quality, or conferencing convenience.

Required implications:

- Latency MUST be favored over feature breadth.
- One-way end-to-end latency budget for rhythm-sensitive sessions MUST target `< 30 ms`.
- Designs that improve average quality while harming timing stability MUST be rejected.
- Timing regressions MUST be treated as first-class failures.

### II. Performance Is Mandatory

The implementation MUST prefer performant solutions in all latency-sensitive and
throughput-sensitive paths.

Required implications:

- The hot path MUST avoid unnecessary heap allocation, copying, dynamic dispatch, and
  generic abstraction overhead.
- Domain boundaries MUST be explicit, but the implementation MUST remain data-oriented
  and cache-conscious.
- Runtime control loops MUST be bounded, measurable, and safe under sustained load.
- Convenience abstractions are acceptable only outside critical transport, synchronization,
  and audio-processing paths.
- Candidate gathering, path scoring, and path switching policy MUST remain outside the
  steady-state media processing path.
- Background reprobe and switching decisions MUST execute asynchronously from the media
  processing path.
- Runtime configuration and logging MUST be externally configurable.
- Production mode MUST support logging level `off` so normal operation can avoid avoidable
  logging overhead.

### III. Direct Media Path by Default

Media MUST flow peer-to-peer over UDP whenever a direct path is possible.

Required implications:

- Signaling infrastructure MAY assist discovery, NAT coordination, role assignment, and
  session bootstrap.
- Signaling infrastructure MUST NOT relay media by default.
- Relay transport is an explicit fallback path, not the standard architecture.

### IV. Public-Internet Reality

The system MUST assume unmanaged networks with NAT, jitter, packet loss, path asymmetry,
and route changes.

Required implications:

- All runtime control loops MUST tolerate imperfect clocks and unstable packet timing.
- All latency claims MUST be framed against real internet paths, not just LAN behavior.
- Path choice MUST account for stability, not latency alone.

### V. Small Buffers, Controlled Adaptation

The system MUST keep jitter buffers as small as practical while remaining stable.

Required implications:

- Buffer growth MUST be deliberate, measured, and reversible.
- Sudden large latency increases are a failure mode, not a valid adaptation strategy.
- Buffer adaptation MUST be driven by packet timing metrics and audible-risk thresholds.

### VI. Continuous Synchronization

Synchronization is a runtime system, not a one-time handshake.

Required implications:

- Clock offset, drift, buffer fill, and playout timing MUST be monitored continuously.
- Long sessions MUST remain stable without manual resets.
- Drift correction MUST be smooth enough to avoid audible artifacts.

### VII. Gentle Drift Correction

Drift MUST be corrected by small continuous rate changes and fractional resampling,
never by abrupt time jumps in normal operation.

Required implications:

- Preferred playback-rate correction range is near-unity (e.g., `0.9999–1.0001`).
- Sample drops, silence insertion, and hard jumps are emergency recovery tools only.
- Drift-control logic MUST prioritize inaudibility.

### VIII. Measurable State

Every critical transport and synchronization mechanism MUST expose metrics.

Required implications:

- Path selection MUST produce RTT, jitter, loss, and stability scores.
- Audio runtime MUST expose buffer fill, reorder, resend, underrun, and drift telemetry.
- The system MUST be debuggable without guesswork.

### IX. Explicit Timing Authority

Even in a P2P media topology, there MUST be a well-defined timing reference.

Required implications:

- Each session MUST assign a master timing authority during setup.
- Followers MUST adapt relative to that reference.
- The master role MUST be visible in session state and transferable if needed.

### X. PCM First, Codec Optional

The default streaming mode is uncompressed PCM unless measured path conditions justify
compression.

Required implications:

- PCM is the baseline for best-case performance and minimum codec delay.
- Low-delay Opus is a conditional fallback for constrained bandwidth or unstable links.
- Codec choice MUST remain a runtime policy decision, not a compile-time assumption.

### XI. Layer Separation

Transport, path management, synchronization, jitter handling, audio framing, and device I/O
MUST remain separable.

Required implications:

- No layer MAY hide timing state that another layer requires for adaptation.
- Cross-layer coupling is allowed only through explicit interfaces and metrics.
- The implementation MUST support replacing one layer without rewriting the entire system.

### XII. Geographic Truthfulness

The system MUST NOT promise sub-30 ms one-way interaction beyond plausible physical limits.

Required implications:

- Approximate fiber propagation cost MUST be treated as a hard budget component.
- Regional proximity is a prerequisite for best musical results.
- Recommendations MUST distinguish transport quality from geography-imposed delay.
- Session guidance MUST explicitly flag when one-way latency exceeds the `< 30 ms` budget.

### XIII. Fail Safe, Not Fake

When conditions do not support rhythm-sensitive collaboration, the system MUST say so
explicitly.

Required implications:

- Reports MUST classify paths as ready, marginal, or not ready.
- The system MUST NOT conceal severe instability behind oversized buffers.
- Operator guidance MUST prefer truth over optimism.

## Required Acceptance Criteria

Any implementation or change is acceptable only if it preserves these invariants:

- P2P UDP media remains the primary path.
- Performance-sensitive paths remain allocation-conscious and low-overhead.
- Runtime metrics remain available for path, transport, and synchronization state.
- One-way latency budget enforcement remains visible in metrics and operator guidance (`< 30 ms` target).
- Clock drift is managed continuously with smooth correction.
- Buffering remains intentionally small and measurable.
- The architecture remains viable on real public-internet links.

## Change Policy

Changes that violate this constitution require an explicit architecture decision record (ADR)
stored under `.specify/adr/` stating:

- which principle is being bent or broken
- why the alternative is necessary
- what measurable downside is introduced
- how the downside will be bounded or removed later

## Governance

This constitution supersedes all other project practices. Amendments require:

1. A proposed ADR documenting the principle change and rationale.
2. Version bump per semantic versioning rules:
   - **MAJOR**: Backward-incompatible principle removals or redefinitions.
   - **MINOR**: New principle or section added or materially expanded.
   - **PATCH**: Clarifications, wording fixes, non-semantic refinements.

All feature plans MUST include a Constitution Check gate verifying compliance with the
principles above before Phase 0 research proceeds.

Operational verification and runbooks MUST explicitly document when commands require
elevated permissions in constrained environments (for example, local sandbox restrictions
that block UDP socket binding during `pj-net-test` execution).

**Version**: 1.0.2 | **Ratified**: 2026-03-15 | **Last Amended**: 2026-03-16
