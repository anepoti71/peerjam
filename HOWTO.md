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

## 2. Build

On each machine:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
cmake -S . -B build
cmake --build build -j 4
```

Expected binaries:

- `build/pj-signal-server`
- `build/pj-net-test`
- `build/pj-peer-probe`

## 3. Configuration Files

Default config files are stored in:

- [signal-server.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/signal-server.conf)
- [net-test.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/net-test.conf)
- [peer-probe.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/peer-probe.conf)

For the specific case where your local machine is the signaling server and another machine is remote, use these prepared examples:

- [local-server-public.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/local-server-public.conf)
- [local-tester-public.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/local-tester-public.conf)
- [remote-tester-public.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/remote-tester-public.conf)

Troubleshooting for that topology is documented in [REMOTE_LOCAL_SERVER_TROUBLESHOOTING.md](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/REMOTE_LOCAL_SERVER_TROUBLESHOOTING.md).

Each binary accepts:

```bash
--config /path/to/file.conf
```

CLI flags still override values from the config file.

## 4. Logging

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

## 5. Start The Signaling Server

Run this on the public host:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
./build/pj-signal-server --config resources/config/signal-server.conf
```

If the public host is your local machine and you want simple start/stop commands, use:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
make build
make server-start
make server-status
make server-logs
make server-stop
```

The `Makefile` uses:

- [local-server-public.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/local-server-public.conf) for `make server-start`
- [local-tester-public.conf](/Users/anepoti/workspace/experiments/audio-jam/peerjam-native/resources/config/local-tester-public.conf) for `make net-test-local`
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

## 6. Run The Network Tester On Both Remote Machines

Use the same `--group` on both machines and different `--user` values.

Machine A:

```bash
./build/pj-net-test \
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
./build/pj-net-test \
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

## 7. What The Tester Produces

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

## 8. How To Read The Test Result

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

## 9. Start The Full Peer Runtime With The Tested Profile

After a successful test, use the profile file from each machine to configure the audio run.

Machine A:

```bash
./build/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user alice \
  --udp-port 12000 \
  --profile-in alice-session-profile.json \
  --audio-seconds 30 \
  --channels 2
```

Machine B:

```bash
./build/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host PUBLIC_SERVER_IP \
  --server-port 10999 \
  --group duo-test \
  --user bob \
  --udp-port 12000 \
  --profile-in bob-session-profile.json \
  --audio-seconds 30 \
  --channels 2
```

What `--profile-in` applies:

- `sample_rate_hz`
- `frame_samples`
- `sink_buffer_ms`
- `source_buffer_ms` derived from the tested initial buffer target

You can still override other runtime values on the CLI if needed.

## 10. Recommended First Real-World Procedure

1. Run the tester on both sites with wired connections.
2. Repeat at least 3 times at different times of day.
3. Compare `session_profile.status` and p95 metrics across runs.
4. If stable and `ready`, run `pj-peer-probe` with the saved profiles.
5. If `marginal`, test again with fewer background network loads before deciding.

## 11. Troubleshooting

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
