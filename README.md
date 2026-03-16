# PeerJam Native

PeerJam Native is the C++ implementation path for remote live-music transport analysis.

## Governance

Project governance lives under [`.specify/`](.specify).
That folder is the source of truth for architecture and change policy:

- constitution: [constitution.md](.specify/memory/constitution.md)
- layer specification: [layer-specification.md](.specify/memory/layer-specification.md)
- verification baseline: [verification.md](.specify/memory/verification.md)
- metrics guidance: [metrics-spec.md](.specify/memory/metrics-spec.md)
- architecture decisions: [ADR README](.specify/adr/README.md)

All future architecture and implementation changes should be evaluated against `.specify/`,
not the removed Speckit layout.

Operational step-by-step usage is documented in [HOWTO.md](HOWTO.md).
Local operator start/stop helpers are available in the top-level [Makefile](Makefile).

Runtime configuration files live under [resources/config](resources/config).

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
- `network`: candidate gathering, candidate exchange, and path selection
- `probe`: probe packets, measurement, readiness logic
- `audio`: AOO PCM runtime and stream telemetry

The split is intentionally performance-oriented: each hot-path domain stays narrow, explicit, and allocation-conscious.
The network domain is deliberately kept off the steady-state audio hot path: candidate gathering and path scoring happen in control time, not per packet.

Current network capabilities:

- local host candidate gathering
- optional STUN server-reflexive candidate gathering
- full local candidate publication through signaling
- remote candidate retrieval through signaling
- signaling-side candidate exchange on `server_port + 1`
- per candidate-pair probe windows
- path scoring and active-path selection
- background reprobe
- hysteresis-based path switching decisions
- active transport/audio adoption of the selected candidate pair
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

## Network tester

Use `pj-net-test` as a preflight qualifier before starting the full peer/audio session.
It reuses the same signaling, candidate discovery, candidate exchange, path probing, and
scoring logic as `pj-peer-probe`, but stops at network qualification and emits a
machine-readable `session_profile`.

Build output includes:
- `pj-signal-server`
- `pj-net-test`
- `pj-peer-probe`
- `pj-net-test` can persist a tested profile with `--profile-out FILE`
- `pj-peer-probe` can consume it with `--profile-in FILE`
- all binaries can load a config file with `--config FILE`

## Build

```bash
cd peerjam-native
cmake -S . -B build
cmake --build build
```

This repository is self-contained for AOO transport sources via `third_party/aoo`.
If you keep AOO elsewhere, override with `-DAOO_ROOT=/absolute/path/to/aoo`.

## Run

Start a public signaling server on a machine reachable from both sites:

```bash
./build/pj-signal-server --config resources/config/signal-server.conf
```

This starts:
- AOO signaling on TCP/UDP `10999`
- candidate exchange on TCP `11000`

On site A:

```bash
./build/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user alice \
  --udp-port 12000
```

On site B:

```bash
./build/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user bob \
  --udp-port 12000
```

After qualification, run the full peer session if the returned `session_profile.status`
is `ready` or an acceptable `marginal`.

## Peer runtime

On site A:

```bash
./build/pj-peer-probe \
  --config resources/config/peer-probe.conf \
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
- `session_profile` for the recommended runtime settings

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
