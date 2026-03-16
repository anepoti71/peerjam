# Remote Tester And P2P How-To

This document explains how to:

1. run the network tester on two remote machines
2. generate a reusable session profile
3. start the peer-to-peer audio runtime with the tested settings
4. use configuration files and configurable logging

## 1. What You Need

- one public machine reachable from both sites to run `pj-signal-server`
- two remote machines, one per musician/site
- the `peerjam-native` build on all three machines
- wired internet if possible
- a STUN server if you want public-internet candidate discovery

Recommended:

- same-region or nearby-region sites
- low-latency audio interfaces for the full peer runtime
- no VPN unless it is part of the intended deployment path

## 2. OS Dependencies

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libopus-dev \
  portaudio19-dev
```

### macOS (Homebrew)

```bash
brew install cmake pkg-config opus portaudio
```

## 3. Build (Linux and macOS)

On each machine:

```bash
cd peerjam-native
cmake -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
cmake --build build-portaudio -j 4
```

If your AOO source tree is not at the default path, set it explicitly:

```bash
cmake -S . -B build-portaudio \
  -DPEERJAM_ENABLE_PORTAUDIO=ON \
  -DAOO_ROOT=/absolute/path/to/aoo
```

Expected binaries:

- `build-portaudio/pj-signal-server`
- `build-portaudio/pj-net-test`
- `build-portaudio/pj-peer-probe`

## 4. Configuration Files

Default config files are stored in:

- [signal-server.conf](resources/config/signal-server.conf)
- [net-test.conf](resources/config/net-test.conf)
- [peer-probe.conf](resources/config/peer-probe.conf)

For the specific case where your local machine is the signaling server and another machine is remote, use these prepared examples:

- [local-server-public.conf](resources/config/local-server-public.conf)
- [local-tester-public.conf](resources/config/local-tester-public.conf)
- [remote-tester-public.conf](resources/config/remote-tester-public.conf)

Troubleshooting for that topology is documented in [REMOTE_LOCAL_SERVER_TROUBLESHOOTING.md](REMOTE_LOCAL_SERVER_TROUBLESHOOTING.md).

Each binary accepts:

```bash
--config /path/to/file.conf
```

CLI flags still override values from the config file.

## 5. Logging

Logging is configured in the same config files with:

- `logging.enabled`
- `logging.level`
- `logging.sink`
- `logging.file_path`

Supported levels:

- `off`
- `error`
- `info`
- `debug`

For production-style runs where logging overhead should be minimized:

```text
logging.enabled = false
logging.level = off
```

## 6. Start The Signaling Server

Run this on the public host:

```bash
cd peerjam-native
./build-portaudio/pj-signal-server --config resources/config/signal-server.conf
```

If the public host is your local machine and you want simple start/stop commands, use:

```bash
cd peerjam-native
make build
make server-start
make server-status
make server-logs
make server-stop
```

The `Makefile` uses:

- [local-server-public.conf](resources/config/local-server-public.conf) for `make server-start`
- [local-tester-public.conf](resources/config/local-tester-public.conf) for `make net-test-local`
- `.run/pid` for PID files
- `.run/log` for local server logs

This starts:

- AOO signaling on TCP/UDP `10999`
- candidate exchange on TCP `11000`

For the local-server example configs, the default ports are:

- signaling: `11999`
- candidate exchange: `12000`

Before starting the local server, you can check whether those ports are already in use:

```bash
make ports-check
```

Make sure both remote machines can reach the public host on those ports.

## 7. Run The Network Tester On Both Remote Machines

Use the same `--group` on both machines and different `--user` values.

Machine A:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user alice \
  --udp-port 12000 \
  --stun-host STUN_SERVER_HOST \
  --stun-port 3478 \
  --duration 8 \
  --interval-ms 10 \
  --payload-bytes 64 \
  --path-reprobe-ms 3000 \
  --path-switch-margin 1.0 \
  --path-switch-wins 2 \
  --profile-out alice-session-profile.json
```

Machine B:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user bob \
  --udp-port 12000 \
  --stun-host STUN_SERVER_HOST \
  --stun-port 3478 \
  --duration 8 \
  --interval-ms 10 \
  --payload-bytes 64 \
  --path-reprobe-ms 3000 \
  --path-switch-margin 1.0 \
  --path-switch-wins 2 \
  --profile-out bob-session-profile.json
```

## 7A. Local Reproducible Test (Single Machine, Loopback)

Use this when you want to verify end-to-end behavior locally before cross-site testing.

1. Ensure the signal server is running:

```bash
make server-start
tail -f .run/log/pj-signal-server.log
```

Expected line:
- `pj-signal-server listening on TCP/UDP port 11999 and candidate exchange TCP port 12000`

2. Avoid startup warning for missing input profile:

Edit [local-tester-public.conf](resources/config/local-tester-public.conf):

```ini
profile.in =
```

3. Start peer A and peer B in two terminals within 15 seconds.
Use the same group value in both terminals:

```bash
GROUP=local-test-20260316-1
```

Terminal A:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-public.conf \
  --server-host 127.0.0.1 \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --duration 2 \
  --interval-ms 20
```

Terminal B:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-public.conf \
  --server-host 127.0.0.1 \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --duration 2 \
  --interval-ms 20
```

4. Verify successful completion:
- both commands exit with code `0`
- both print final JSON containing `network`, `probe`, and `session_profile`

5. If commands fail to bind UDP ports, clear stale processes and retry:

```bash
lsof -nP -iUDP:12110
lsof -nP -iUDP:12111
kill <PID_FROM_LSOF>
```

6. Constrained environments:
- If local sandboxing blocks UDP socket bind/connect, run `pj-net-test` with elevated permissions.
- Documented requirement: see `.specify/memory/constitution.md` Governance section.

## 7B. Local Smoke Profile (Expect `ready` For Reproducibility)

Use this only for local pipeline validation. It uses relaxed recommendation thresholds.

1. Set a shared group value:

```bash
GROUP=local-smoke-20260316-1
```

2. Run in two terminals:

Terminal A:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-smoke.conf \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110
```

Terminal B:

```bash
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-smoke.conf \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111
```

3. Expected for smoke pass:
- both commands exit `0`
- `session_profile.status` is typically `ready` on both peers

Important:
- This profile is not a production/cross-site acceptance gate.
- For real readiness, use `local-tester-public.conf` or remote configs and default thresholds.

## 8. What The Tester Produces

Each tester prints JSON with:

- `network`
- `probe`
- `session_profile`

It also writes the `session_profile` object to the file passed with `--profile-out`.

The profile contains:

- `status`
- `codec`
- `sample_rate_hz`
- `frame_samples`
- `initial_buffer_ms`
- `rationale`

## 9. How To Read The Test Result

Focus on:

- `probe.estimated_one_way_ms.p95_ms`
- `probe.ingress_jitter_ms.p95_ms`
- `probe.loss_percent`
- `session_profile.status`

Guidance:

- `ready`
  - good candidate for live remote music
- `marginal`
  - may work, but timing margin is limited
- `not_ready`
  - do not expect reliable rhythm-sensitive collaboration

Good signs:

- one-way p95 well below `30 ms`
- low jitter p95
- very low packet loss
- stable candidate-pair selection

## 10. Start The Full Peer Runtime With The Tested Profile

After a successful test, use the profile file from each machine to configure the audio run.

Machine A:

```bash
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user alice \
  --udp-port 12000 \
  --profile-in alice-session-profile.json \
  --audio-seconds 30 \
  --channels 2 \
  --audio-device-io \
  --audio-input-device -1 \
  --audio-output-device -1 \
  --ui-telemetry-port 17070 \
  --ui-telemetry-interval-ms 100
```

Machine B:

```bash
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user bob \
  --udp-port 12000 \
  --profile-in bob-session-profile.json \
  --audio-seconds 30 \
  --channels 2 \
  --audio-device-io \
  --audio-input-device -1 \
  --audio-output-device -1 \
  --ui-telemetry-port 17070 \
  --ui-telemetry-interval-ms 100
```

What `--profile-in` applies:

- `sample_rate_hz`
- `frame_samples`
- `sink_buffer_ms`
- `source_buffer_ms` derived from the tested initial buffer target

You can still override other runtime values on the CLI if needed.

## 11. Realtime UI For End-to-End Audio Validation

On each peer machine:

```bash
cd ui/peerjam-monitor
python3 -m http.server 8088
```

Open `http://127.0.0.1:8088` and:

1. Set realtime endpoint to `http://127.0.0.1:17070/events`.
2. Click `Connect`.
3. Click `Refresh Devices`.
4. Choose `C++ Input Device` and `C++ Output Device`.
5. Click `Apply Devices` before audio run starts.

What to verify:

- capture waveform is active (local input)
- received waveform is active (remote peer audio)
- latency and jitter telemetry stay stable
- one-way latency remains below the mandatory `< 30 ms` budget

## 12. Latency Budget Requirement

For rhythm-sensitive remote collaboration, treat `< 30 ms` one-way latency as mandatory.

- if one-way latency exceeds `30 ms`, classify as not suitable for timing-critical play
- if jitter/loss are unstable, treat as not-ready even when average latency looks acceptable

## 13. Recommended First Real-World Procedure


1. Run the tester on both sites with wired connections.
2. Repeat at least 3 times at different times of day.
3. Compare `session_profile.status` and p95 metrics across runs.
4. If stable and `ready`, run `pj-peer-probe` with the saved profiles.
5. If `marginal`, test again with fewer background network loads before deciding.

## 14. Troubleshooting

If no direct path is established:

- verify both sites can reach the public signaling server
- verify the signaling server ports are open
- add STUN if it is not already enabled
- check whether local firewalls block UDP

If the tester works but the peer runtime is unstable:

- use the saved profile instead of manual values
- reduce competing traffic on both sites
- prefer wired networking
- keep both sites geographically close when possible

If geography is too large:

- expect higher one-way latency even with a clean network
- the tester may still show a good path relative to available candidates, but the result can remain `marginal` or `not_ready` for live ensemble timing
