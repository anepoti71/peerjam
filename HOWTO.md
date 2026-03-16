# PeerJam Native HOWTO

Single runbook for build, qualification, and runtime on:

- single-machine local smoke
- two-machine LAN
- remote internet peers

## 1. Prerequisites

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopus-dev portaudio19-dev
```

### macOS (Homebrew)

```bash
brew install cmake pkg-config opus portaudio
```

## 2. Clean Build (all machines)

```bash
cd /path/to/peerjam-native
rm -rf build-portaudio/CMakeCache.txt build-portaudio/CMakeFiles
cmake --fresh -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
cmake --build build-portaudio -j 4
```

Expected binaries:

- `build-portaudio/pj-signal-server`
- `build-portaudio/pj-net-test`
- `build-portaudio/pj-peer-probe`

If you see cache/source mismatch errors after copying repos between machines, remove `build-portaudio` and reconfigure.

## 3. Scenario Setup

Use one of these setups:

1. Local smoke (single machine):
- server host: `127.0.0.1`
- peers: two terminals
- config baseline: `resources/config/local-tester-smoke.conf`

2. Two-machine LAN:
- server host: machine A IP (example `192.168.0.21`)
- machine A user: `alice`
- machine B user: `bob`
- config baseline: `resources/config/net-test.conf` and `resources/config/peer-probe.conf`

3. Remote internet:
- server host: public IP/DNS reachable by both peers
- optional STUN host if needed
- same command pattern as LAN with public host values

## 4. Start Signaling Server

On the server machine:

```bash
cd /path/to/peerjam-native
./build-portaudio/pj-signal-server --config resources/config/signal-server.conf
```

Or with helper targets:

```bash
make server-start
make server-status
make server-logs
```

Default ports (depending on config):

- signaling: TCP/UDP `10999` or `11999`
- candidate exchange: TCP `11000` or `12000`

## 5. Run Network Qualification (`pj-net-test`)

Rules:

- same `--group` on both peers
- different `--user`
- different `--udp-port` when peers run on different hosts/terminals

Set a shared group:

```bash
GROUP=duo-test-$(date +%Y%m%d-%H%M%S)
```

Peer A:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host SERVER_HOST \
  --server-port SERVER_PORT \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --profile-out alice-session-profile.json
```

Peer B:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host SERVER_HOST \
  --server-port SERVER_PORT \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --profile-out bob-session-profile.json
```

Optional tuning:

- `--interval-ms 20`
- `--payload-bytes 32`
- `--stun-host HOST --stun-port 3478`

## 6. Read Qualification Output

Primary metrics:

- `probe.estimated_one_way_ms.p95_ms`
- `probe.ingress_jitter_ms.p95_ms`
- `probe.loss_percent`
- `session_profile.status`

Interpretation:

- `ready`: usable for live music sessions
- `marginal`: can work, timing margin limited
- `not_ready`: not reliable for rhythm-sensitive sessions

Target for rhythm-sensitive sessions: one-way p95 under `30 ms`.

## 7. Run Full Peer Runtime (`pj-peer-probe`)

Use a fresh group:

```bash
GROUP=duo-audio-$(date +%Y%m%d-%H%M%S)
```

Peer A:

```bash
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host SERVER_HOST \
  --server-port SERVER_PORT \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --profile-in alice-session-profile.json \
  --audio-seconds 120
```

Peer B:

```bash
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host SERVER_HOST \
  --server-port SERVER_PORT \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --profile-in bob-session-profile.json \
  --audio-seconds 120
```

If Opus support differs between machines, force PCM for both peers:

```bash
--codec pcm --profile-in ""
```

## 8. Predictor (Hybrid Reactive + Predictive)

Config keys:

- `predictor.enabled`
- `predictor.shadow_mode`
- `predictor.interval_ms`
- `predictor.risk_medium_threshold`
- `predictor.risk_high_threshold`
- `predictor.apply_cooldown_ms`
- `predictor.max_sink_buffer_ms`

Recommended rollout:

1. Start in shadow mode:
- `predictor.shadow_mode = true`

2. Enable active mitigation after validating telemetry:
- `--predictor-shadow-mode false`
- `--predictor-apply-cooldown-ms 1200`
- `--predictor-max-sink-buffer-ms 12`

Realtime telemetry `predictor.apply_status` values:

- `0`: no action
- `1`: applied
- `2`: shadow mode
- `3`: cooldown
- `4`: codec locked
- `5`: no-op

## 9. UI Monitoring

Start UI server on each machine:

```bash
cd ui/peerjam-monitor
python3 -m http.server 8088
```

Open:

- `http://127.0.0.1:8088`

Run peer with UI telemetry enabled:

- peer A: `--ui-telemetry-port 17070`
- peer B: `--ui-telemetry-port 17071`

In UI:

1. Set endpoint to `http://127.0.0.1:17070/events` (or `17071` on second machine)
2. Connect
3. Refresh devices
4. Select C++ input/output devices
5. Apply before audio starts

## 10. Troubleshooting

No remote peer joined:

- same `--group` on both peers
- both peers started within ~15 seconds
- verify reachability to signaling and candidate exchange ports

Server fails to start on candidate port:

```bash
lsof -nP -iTCP:12000 -sTCP:LISTEN
make server-stop
```

CMake path mismatch after moving repo:

```bash
rm -rf build-portaudio
cmake --fresh -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
```

Linux `Exec format error`:

- remove copied binaries and rebuild natively on Linux

No audio devices / ALSA errors:

- verify host audio stack (ALSA/Pulse/PipeWire)
- on headless hosts, use network qualification mode first (`pj-net-test`)

## 11. Elevated Permissions Guidance

In constrained environments (sandboxed terminals, policy-restricted CI, hardened desktops), UDP bind/connect or local port access may require elevated permissions.

If a command fails due environment restrictions (not due app logic), rerun that command with elevated permission and document it in test notes.

