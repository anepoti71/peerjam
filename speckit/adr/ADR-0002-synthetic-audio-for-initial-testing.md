# ADR-0002: Synthetic Tone for Initial Integration Testing

## Status

Accepted

## Date

2026-03-15

## Principle Affected

No constitutional principle is violated. This decision is scoped to the development and testing phase and does not affect production system behavior.

## Context

Layer 9 (Audio Device) requires low-latency native audio device capture and playback. Integrating real device I/O (CoreAudio on macOS, ALSA/PipeWire on Linux) during initial development:
- Requires hardware availability in CI environments
- Adds device driver complexity before the network and synchronization layers are stable
- Makes automated regression testing difficult

## Decision

`AudioRuntime` generates a synthetic sine-wave tone as its audio source for the current development phase. The tone is configurable (frequency, level) via `PeerOptions`. The receiver sink processes AOO audio blocks but does not route them to a real output device.

## Consequences

### Which principle is being bent or broken

No constitutional principle is violated. The layer specification (Layer 9) describes requirements for a production implementation. The synthetic audio path is an explicit placeholder.

### Why the alternative is necessary

CI/automated testing requires deterministic, hardware-independent audio input. A synthetic source provides:
- Reproducible signal for metric validation
- No dependency on audio hardware in test environments
- Ability to test the full transport/sync pipeline without device variance

### Measurable downside introduced

1. End-to-end latency does not include real device capture/playback latency.
2. Acoustic echo and device clock drift — which are primary real-world complications — are not exercised.
3. The `AudioStreamReport` metrics reflect synthetic block timing, not real device callback timing.

### How the downside will be bounded or removed later

This ADR is superseded when real device I/O is integrated. The integration point is `audio_runtime.cpp`. The `setup()` and `run()` methods should be extended to:
1. Open a real audio device using the platform's native low-latency API.
2. Replace the sine-wave generator with a real capture callback.
3. Route the sink output to a real playback device.

Device latency must then be measured and added to the end-to-end budget report. A new ADR is not required for this change as it advances toward — not away from — the constitutional requirements.
