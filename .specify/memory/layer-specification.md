# PeerJam Native Layer Specification

## Scope

This document translates the constitution into concrete specifications for each architectural
layer of a low-latency P2P live-music system.

## 1. Session and Control Layer

### Responsibilities

- authenticate or identify peers for a session
- perform peer discovery
- coordinate NAT traversal bootstrap
- elect the session timing authority
- distribute initial session parameters

### Required behavior

- The control server may participate in setup but must not relay media by default.
- The control plane must support candidate publication and retrieval so each peer can obtain
  the other side's host and server-reflexive candidates without placing media on the server
  path.
- Session and audio runtime parameters must be loadable from configuration files under
  `resources/config/`, with CLI overrides allowed where operationally useful.
- Session setup must assign:
  - session ID
  - peer IDs
  - master clock authority
  - target sample rate
  - preferred frame size
  - initial jitter-buffer target
  - codec policy
- A control-plane disconnect must not immediately tear down an already-established direct
  media path.

### Interfaces

Inputs:

- peer identity
- network candidates
- local capabilities

Outputs:

- selected peer endpoints
- full remote candidate set
- timing authority selection
- negotiated stream parameters

## 2. Candidate Discovery and NAT Layer

### Responsibilities

- gather local, reflexive, and optional relay candidates
- coordinate hole punching
- maintain candidate metadata

### Candidate classes

- host candidates: local interface addresses
- server-reflexive candidates: STUN-discovered public addresses
- IPv4 candidates
- IPv6 candidates
- relay candidates as explicit fallback

### Required behavior

- Candidate discovery must complete before media-path scoring begins.
- Candidates must be retained with provenance and freshness timestamps.
- Candidate exchange must publish and retrieve full candidate sets efficiently, outside the
  steady-state media hot path.
- Relay candidates must remain available but de-prioritized unless direct paths fail.

### Metrics

- candidate type
- observed reachability
- successful probe count
- last success time

## 3. Path Probing and Selection Layer

### Responsibilities

- probe candidate paths
- estimate latency, jitter, loss, and short-term stability
- select the best active path
- monitor alternates in the background

### Probe requirements

Each probe packet must carry:

- sequence number
- send timestamp
- session token
- probe type

Measured metrics:

- RTT
- estimated one-way latency
- arrival jitter
- loss
- reordering
- stability over a moving window

### Scoring model

The reference scoring model is:

`score = latency_ms + 2 * jitter_ms + loss_penalty`

Where:

- `latency_ms` is based on a moving percentile, not only the mean
- `jitter_ms` is based on short-window arrival variation
- `loss_penalty` increases aggressively once loss becomes audible risk

### Path switching policy

- Background probing of alternate candidates must continue after path selection.
- Path switching requires hysteresis.
- Switching is allowed only when a candidate is materially better for a sustained interval.
- Path switching must coordinate with jitter-buffer state to avoid audible disruption.
- When a switch is accepted, the active local transport bind and active remote media endpoint
  must adopt the selected candidate pair instead of reporting the decision only.

## 4. Media Transport Layer

### Responsibilities

- send and receive timestamped audio packets over UDP
- route transport-control packets separately from session-control packets
- preserve low-overhead packetization

### Required behavior

- Media transport must operate directly between peers whenever possible.
- Packet headers must include:
  - sequence number
  - media timestamp
  - stream ID
  - codec/format identity
  - payload frame index when fragmented
- The transport must support:
  - PCM baseline mode
  - low-delay Opus fallback mode
  - packet resend requests where applicable
  - packet reordering

### Packetization targets

- preferred sample rate: `48 kHz`
- preferred frame size: `2.5-5 ms`
- acceptable initial operating frame size: `128` samples at `48 kHz`
- channel mode: mono or stereo

## 5. Audio Framing and Codec Layer

### Responsibilities

- convert audio blocks into network frames
- keep codec delay minimal
- align framing with transport timing

### PCM mode

- default operating mode for the best path
- float32 or equivalent internal precision
- minimal algorithmic delay

### Opus low-delay mode

- fallback for constrained or unstable paths
- restricted low-delay mode only
- must not become default for clean low-latency regional links

### Constraints

- frame sizing must remain small
- codec choice must be adjustable per path and session
- total codec-added latency must remain visible in reports

## 6. Jitter Buffer Layer

### Responsibilities

- reorder packets
- absorb short-term network variation
- feed the playout clock

### Required behavior

- buffer target must remain as small as possible while stable
- target operating range: `5-20 ms`
- the buffer must expose:
  - fill ratio
  - underrun count
  - overrun count
  - reorder count
  - missing frame count

### Adaptation policy

- increase buffer size only when persistent jitter threatens playout continuity
- decrease buffer size slowly after stability returns
- never hide systemic latency problems behind unchecked buffer growth

## 7. Clock Synchronization Layer

### Responsibilities

- establish initial offset estimate
- track relative clock drift
- maintain a shared notion of playout timing

### Initial synchronization

- estimate peer offset during setup
- optionally incorporate NTP-disciplined clocks when available
- record initial uncertainty, not just the offset estimate

### Runtime synchronization

- continuously monitor timestamp deltas
- maintain moving estimates for:
  - clock offset
  - effective sample-rate mismatch
  - playout offset from target

### Timing authority

- one peer or playback node must be the reference clock
- all followers must adapt to the reference
- timing authority must be transferable if the leader leaves

## 8. Drift Detection and Correction Layer

### Responsibilities

- detect long-term clock mismatch
- correct drift without audible artifacts

### Required method

- use continuous playback-rate adjustment
- implement fractional resampling
- keep correction small and smooth

### Operating guidance

- nominal correction range is close to unity
- corrections should be applied continuously over time
- abrupt sample drops or silence insertion are emergency recovery tools only

### Required telemetry

- estimated drift in ppm
- current playback-rate multiplier
- long-window buffer-fill trend
- number of emergency recoveries

## 9. Audio Device Layer

### Responsibilities

- capture and play audio with minimal host latency
- keep callback timing deterministic
- bridge device time and media time

### Required behavior

- use low-latency native APIs
- avoid large host buffers
- keep callback size aligned with media frame size where practical
- expose actual device latency as part of the end-to-end budget
- support explicit input-device and output-device selection at runtime setup
- expose realtime captured and received waveform telemetry for operator validation

### Non-goals

- no heavyweight mixer or DAW-style graph in the critical path
- no hidden resampling outside the synchronization layer

## 10. Monitoring and Policy Layer

### Responsibilities

- aggregate live transport and synchronization metrics
- drive adaptation policies
- expose operational guidance

### Required metrics

- RTT and one-way estimates
- jitter
- loss
- reorder
- resend counts
- jitter-buffer fill ratio
- drift estimate
- playout-rate adjustment
- active path score
- one-way latency budget breach flag (`> 30 ms`)

### Policy outputs

- ready / marginal / not-ready classification
- recommended frame size
- recommended codec
- recommended initial jitter buffer
- recommendation to re-elect master or resynchronize
- machine-readable preflight session profile for the peer runtime
- explicit warning when one-way latency exceeds the `< 30 ms` interaction budget

## 11. Configuration and Logging Layer

### Responsibilities

- externalize runtime tunables and startup parameters
- provide configurable operational logging
- permit production runs with logging disabled
- provide optional realtime UI telemetry endpoint configuration for local monitoring clients

### Required behavior

- Client, tester, and server applications must load startup configuration from files.
- The default project configuration files must live under `resources/config/`.
- Build configuration must support both Linux and macOS without hardcoded platform-specific
  library paths.
- Third-party codec/device dependencies must be discoverable via system package managers
  (e.g., `pkg-config`, standard include/lib discovery), with explicit override options.
- Logging must support at least:
  - `off`
  - `error`
  - `info`
  - `debug`
- Logging sink must be configurable between `stderr` and file output.
- Logging must be disableable in production without changing code.

## 12. Geographic and Deployment Constraints

### Physical guidance

- approximate propagation cost is around `5 ms / 1000 km`
- inter-regional and intercontinental paths consume a large fraction of the latency budget
  before software is considered

### Operational guidance

- best results are expected when peers are in the same metro or region
- the system must report when geography alone makes sub-30 ms interaction unlikely

## 13. Reference Runtime Profile

The preferred starting profile for live music is:

- transport: direct P2P UDP
- sample rate: `48 kHz`
- frame size: `128` samples initially, lower if the path supports it
- channels: mono or stereo
- codec: PCM first
- jitter buffer: start around `5 ms`, expand only when justified
- drift control: enabled by default
- path management: alternate-path probing active in background

## 14. Network Qualification Application

### Responsibilities

- run preflight path qualification before a live music session starts
- reuse the production candidate, transport, and scoring layers
- emit a recommended session profile without requiring audio streaming

### Required behavior

- The tester application must reuse the same probe and path-selection logic as the peer runtime.
- It must not introduce a separate transport stack or a divergent scoring model.
- It must output:
  - selected candidate pair
  - measured path metrics
  - ready / marginal / not-ready status
  - recommended codec, frame size, sample rate, and initial buffer target

## 13. Implementation Notes for PeerJam Native

The current project maps to these layers as follows:

- signaling and peer bootstrap: `src/signal_server_main.cpp`
- peer orchestration and control-plane coordination: `src/logic/control/aoo_peer_client.cpp`
- candidate gathering, exchange, and active-path selection: `src/logic/network/candidate_gatherer.cpp`,
  `src/logic/network/candidate_exchange.cpp`, `src/logic/network/path_manager.cpp`,
  `src/logic/network/path_scorer.cpp`
- UDP transport ownership: `src/logic/transport/udp_datagram_socket.cpp`
- probe transport runtime: `src/logic/probe/probe_runtime.cpp`
- probe packet format: `src/logic/probe/probe_packet.cpp`
- readiness metrics: `src/logic/probe/measurement.cpp`
- AOO PCM runtime: `src/logic/audio/audio_runtime.cpp`
- PCM stream telemetry: `src/logic/audio/audio_stream.cpp`

Immediate next implementation steps:

1. Apply actual media-path rebinding/switching when the path manager elects a better path.
2. Extract a dedicated session-control state machine from the current orchestrator.
3. Add explicit master-clock election and expose the selected role in runtime reports.
4. Replace the synthetic source with real device capture/playback while preserving the
   current telemetry surface.
