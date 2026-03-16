# Quick 2-Machine Test (192.168.0.21 <-> 192.168.0.58)

Complete reproducible flow for LAN testing with fixed roles:

- Machine A (macOS): `192.168.0.21` (signal server + alice)
- Machine B (Linux): `192.168.0.58` (bob)

Phases:

1. Clean build on each machine
2. Signal server on `.21`
3. Network qualification (`pj-net-test`)
4. Audio runtime (`pj-peer-probe`) with forced PCM
5. Optional UI telemetry

---

## 1) Clean build on both machines

Run from repo root on each machine.

```bash
cd /path/to/peerjam-native

# Important when switching OS/machines or copying worktrees
rm -rf build build-portaudio .run

cmake --fresh -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
cmake --build build-portaudio -j 4
```

Linux sanity check:

```bash
file ./build-portaudio/pj-peer-probe
# must be ELF (not Mach-O)
```

---

## 2) Start signaling server on 192.168.0.21

On Machine A (`192.168.0.21`):

```bash
cd /path/to/peerjam-native
make server-stop || true
make server-start
tail -f .run/log/pj-signal-server.log
```

Expected line:

- `pj-signal-server listening on TCP/UDP port 11999 and candidate exchange TCP port 12000`

Network requirements from `.58` to `.21`:

- TCP/UDP `11999`
- TCP `12000`

---

## 3) Network qualification (required)

Use the same group on both machines.

```bash
GROUP=local-test-2
```

Machine A (`192.168.0.21`, alice):

```bash
cd /path/to/peerjam-native
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host 192.168.0.21 \
  --server-port 11999 \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --interval-ms 20 \
  --payload-bytes 32 \
  --profile-out alice-session-profile.json
```

Machine B (`192.168.0.58`, bob):

```bash
cd /path/to/peerjam-native
./build-portaudio/pj-net-test \
  --config resources/config/net-test.conf \
  --server-host 192.168.0.21 \
  --server-port 11999 \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --interval-ms 20 \
  --payload-bytes 32 \
  --profile-out bob-session-profile.json
```

Success criteria:

- `selected_local`/`selected_remote` are LAN addresses (not loopback)
- `loss_percent` near `0`
- `session_profile.status` = `ready`

---

## 4) Audio runtime test (`pj-peer-probe`)

Use a new shared group:

```bash
GROUP=local-audio-1
```

Force PCM on both peers and ignore profile input:

Machine A (`192.168.0.21`, alice):

```bash
cd /path/to/peerjam-native
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host 192.168.0.21 \
  --server-port 11999 \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --audio-seconds 120 \
  --sample-rate 48000 \
  --frame-samples 128 \
  --sink-buffer-ms 5 \
  --codec pcm \
  --profile-in ""
```

Machine B (`192.168.0.58`, bob):

```bash
cd /path/to/peerjam-native
./build-portaudio/pj-peer-probe \
  --config resources/config/peer-probe.conf \
  --server-host 192.168.0.21 \
  --server-port 11999 \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --audio-seconds 120 \
  --sample-rate 48000 \
  --frame-samples 128 \
  --sink-buffer-ms 5 \
  --codec pcm \
  --profile-in ""
```

Predictive mitigation mode:

- default config runs predictor in shadow mode (`predictor.shadow_mode = true`)
- to enable active mitigation for this run, add:
  - `--predictor-shadow-mode false`
  - `--predictor-apply-cooldown-ms 1200`
  - `--predictor-max-sink-buffer-ms 12`

UI telemetry `predictor.apply_status` values:

- `0`: no action requested
- `1`: mitigation applied
- `2`: shadow mode (suggestion only)
- `3`: cooldown gate active
- `4`: blocked by codec lock
- `5`: no-op (already at limit / not applicable)

---

## 5) Optional UI telemetry

On each machine, serve UI files:

```bash
cd /path/to/peerjam-native/ui/peerjam-monitor
python3 -m http.server 8088
```

Browser URLs:

- Machine A UI: `http://127.0.0.1:8088/index.html?ts=1`
- Machine B UI: `http://127.0.0.1:8088/index.html?ts=1`

Peer telemetry endpoints:

- A: `http://127.0.0.1:17070/events`
- B: `http://127.0.0.1:17071/events`

If needed, run peer-probe with:

- A: `--ui-telemetry-port 17070`
- B: `--ui-telemetry-port 17071`

---

## 6) Fast troubleshooting

If `no remote peer joined the group`:

- same `--group` on both sides
- start both peers within 10-15 seconds
- verify `.58 -> .21` connectivity:

```bash
nc -vz 192.168.0.21 11999
nc -vz 192.168.0.21 12000
```

If server fails with candidate port error on `.21`:

```bash
lsof -nP -iTCP:12000 -sTCP:LISTEN
make server-stop
```

If Linux shows `Exec format error`:

- you are running a macOS binary; remove `build-portaudio` and rebuild natively.

If `codec 'opus' not supported!` appears:

- rebuild both machines from latest source
- ensure both commands include `--codec pcm --profile-in ""`

---

## 7) Cleanup

Machine A (`192.168.0.21`):

```bash
cd /path/to/peerjam-native
pkill -f pj-peer-probe || true
pkill -f "python3 -m http.server 8088" || true
make server-stop
```

Machine B (`192.168.0.58`):

```bash
cd /path/to/peerjam-native
pkill -f pj-peer-probe || true
pkill -f "python3 -m http.server 8088" || true
```
