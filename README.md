# PeerJam Native

PeerJam Native is the C++ implementation path for remote live-music transport analysis.

It reuses:

- SonoBus/AOO for transport:
  - public signaling and group join
  - UDP hole punching
  - direct peer endpoint discovery
- JackTrip for performance goals:
  - direct media path bias
  - small frame discipline
  - latency-first recommendation logic

## Code layout

Domain logic now lives under `src/logic` and `include/peerjam/logic`:

- `control`: signaling and peer orchestration
- `transport`: UDP socket and datagram path
- `network`: candidate gathering and path selection
- `probe`: probe packets, measurement, readiness logic
- `audio`: AOO PCM runtime and stream telemetry

The split is intentionally performance-oriented: each hot-path domain stays narrow, explicit, and allocation-conscious.
The network domain is deliberately kept off the steady-state audio hot path: candidate gathering and path scoring happen in control time, not per packet.

Current network capabilities:

- local host candidate gathering
- optional STUN server-reflexive candidate gathering
- full local candidate publication through signaling
- remote candidate retrieval through signaling
- per candidate-pair probe windows
- path scoring and active-path selection
- background reprobe
- hysteresis-based path switching decisions
- active-path reporting

## Why C++ instead of Rust

For this specific project, C++ is the better starting point because the transport layer you want to keep is already AOO, which is native C/C++. Reusing it directly avoids an extra FFI boundary in the hottest integration path.

Rust would be reasonable later for orchestration or control layers, but not as the first implementation if the goal is fastest path to a production-grade P2P transport probe.

## What this native tool does

1. Runs an AOO signaling server on a public host.
2. Runs two peers from different locations.
3. Uses AOO group join and peer discovery to establish a direct path.
4. Streams probe packets peer-to-peer.
5. Measures:
   - direct path establishment
   - RTT
   - estimated one-way latency
   - ingress jitter
   - echo jitter
   - packet loss
   - packet reordering
6. Optionally runs a real PCM streaming phase over AOO source/sink.
7. Produces recommended settings for a target `<30 ms` one-way music workflow.

## Build

```bash
cd peerjam-native
cmake -S . -B build
cmake --build build
```

## Run

Start a public signaling server on a machine reachable from both sites:

```bash
./build/pj-signal-server --port 10999
```

On site A:

```bash
./build/pj-peer-probe \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user alice \
  --udp-port 12000
```

On site B:

```bash
./build/pj-peer-probe \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user bob \
  --udp-port 12000
```

## Output

The peer prints a JSON report with:

- path status
- latency summary
- jitter summary
- packet loss and reorder counts
- recommended sample rate
- recommended audio frame size
- recommended codec mode
- recommended initial network buffer
- feasibility for sub-30 ms live music

If `--audio-seconds` is set, the report also includes:

- rendered PCM block counts
- active audio block counts
- AoO sink play/stop events
- AoO block loss / reorder / resend / gap counters
- sink buffer fill ratio telemetry
- approximate residual drift estimate in ppm
- decoded output RMS and peak summaries

The current streaming phase uses a synthetic PCM tone generator so the transport, jitter buffer, resend path, and drift handling can be measured without introducing platform-specific audio-device code yet.

## PCM streaming mode

Run the normal probe and then a real PCM stream:

```bash
./build/pj-peer-probe \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --stun-host STUN_SERVER_IP_OR_NAME \
  --stun-port 3478 \
  --group duo-test \
  --user alice \
  --udp-port 12000 \
  --audio-seconds 10 \
  --sample-rate 48000 \
  --frame-samples 128 \
  --channels 2 \
  --source-buffer-ms 3 \
  --sink-buffer-ms 5
```

Use the same settings on the second site with a different `--user`.

## Production direction

After this phase:

1. replace the synthetic tone with real audio device capture/playback
2. add low-delay Opus fallback
3. expose adaptive buffer policy instead of static CLI values
4. add session coordinator for master-clock policy
