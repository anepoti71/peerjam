# Logic Domains

This folder contains the latency-sensitive domain logic, split by responsibility.

- `control`
  - session bootstrap
  - signaling client coordination
  - peer orchestration
  - top-level run loop
  - network qualification workflow shared by tester and peer runtime

- `transport`
  - UDP socket ownership
  - low-overhead datagram send/receive
  - endpoint string formatting

- `network`
  - candidate gathering
  - full candidate exchange through signaling
  - STUN server-reflexive discovery
  - candidate-pair probe windows
  - path scoring
  - active path selection
  - background reprobe and hysteresis-based switching policy
  - selected-path adoption for the live transport/audio path

- `probe`
  - probe packet wire format
  - latency/jitter/loss measurement
  - readiness scoring inputs

- `audio`
  - AOO source/sink runtime
  - PCM streaming loop
  - audio telemetry and drift-related stream metrics

Design rule:
- code in these domains must stay allocation-conscious and low-overhead on hot paths
- orchestration may call across domains, but packet processing and audio ticks should remain direct and predictable
- network candidate gathering and scoring must stay outside the steady-state media hot path
