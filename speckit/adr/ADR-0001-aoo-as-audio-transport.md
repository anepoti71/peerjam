# ADR-0001: Use AOO as the Audio Transport and Framing Layer

## Status

Accepted

## Date

2026-03-15

## Principle Affected

Principles 2 (Performance Is Mandatory) and 11 (Layer Separation).

## Context

PeerJam Native requires:
- UDP-based audio packetization with sequence numbers and timestamps
- Jitter buffer with configurable target
- Dynamic resampling for drift correction
- Event reporting for loss, reorder, resend, and gap

Building all of this from scratch in the initial phase would delay functional testing of the path management and synchronization layers, which are the novel contributions of this project.

AOO (Audio Over OSC) is an open-source C library that provides UDP audio transport, PCM framing, jitter buffering, dynamic resampling, and event callbacks in a single integration unit.

## Decision

Use AOO as the implementation of the Media Transport Layer (Layer 4), Audio Framing Layer (Layer 5), and Jitter Buffer Layer (Layer 6) for the current development phase.

AOO is integrated via `audio_runtime.cpp`, which wraps `aoo_source` and `aoo_sink` and exposes the minimum surface required by `AooPeerClient`.

## Consequences

### Which principle is being bent or broken

**Principle 11 (Layer Separation)** is partially bent: layers 4, 5, and 6 are collapsed into a single external library. The jitter buffer target is configurable at setup time (`sink_buffer_ms`) but its internal adaptation policy is not accessible. There is no API to read underrun/overrun counts separately from AOO's internal state; these must be inferred from event callbacks.

**Principle 2 (Performance Is Mandatory)** is potentially affected: AOO's internal implementation allocates memory and performs work whose overhead is not directly measurable without instrumentation. The audio run loop in `audio_runtime.cpp` allocates temporary `std::vector<float>` buffers on each iteration, which is an accepted temporary overhead for the synthetic testing phase.

### Why the alternative is necessary

Building a custom jitter buffer, fractional resampler, and UDP framing layer from scratch is a significant undertaking. Using AOO allows the path management, synchronization, and measurement layers to reach a testable state faster. The AOO integration is isolated behind the `AudioRuntime` wrapper, which limits its surface area.

### Measurable downside introduced

1. Jitter buffer adaptation is opaque: buffer expansion and contraction policy is AOO's, not ours. We cannot implement the constitution's required adaptive controller (Principle 5) without a replacement or extension.
2. Drift correction is partially opaque: AOO provides `dynamic_resampling` but the rate multiplier it applies is not exposed via a public API in the current integration, so the correction feedback loop (Principle 7) cannot be completed without API extension.
3. Codec switching (Principle 10) requires reconfiguring the AOO source format and negotiating with the remote sink, which involves AOO-internal state that must be understood and coordinated.

### How the downside will be bounded or removed later

The `AudioRuntime` wrapper exists precisely to hide AOO's API surface. When the gaps above become blocking:
- Buffer adaptation controller: instrument AOO event callbacks to approximate fill/drain counts; or replace AOO's jitter buffer with a custom implementation while retaining AOO's framing.
- Drift correction loop: extend the integration to call AOO's `aoo_sink_set_resample_threshold` or equivalent to pass the computed drift estimate.
- Codec switching: implement format renegotiation through the existing control channel by adding a codec-change message to the signaling protocol.

If AOO becomes a bottleneck or its opacity prevents completing the required runtime control loops, an ADR will be raised to define a replacement path.
