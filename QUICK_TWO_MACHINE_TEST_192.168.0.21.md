# Quick 2-Machine Test (Server IP: 192.168.0.21)

This is a complete, minimal test plan for two machines on the same LAN:

- Machine A (server + peer + browser): `192.168.0.21`
- Machine B (peer + browser): another LAN IP

The flow has 2 phases:

1. Network qualification (`pj-net-test`)
2. Audio + UI telemetry (`pj-peer-probe`)

---

## 1) Prerequisites on both machines

Run from repo root:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
cmake -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
cmake --build build-portaudio -j 4
```

Network requirements:

- `192.168.0.21:11999` TCP/UDP reachable from Machine B
- `192.168.0.21:12000` TCP reachable from Machine B

---

## 2) Start signaling server on Machine A (192.168.0.21)

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
make server-stop || true
make server-start
tail -f .run/log/pj-signal-server.log
```

Expected log line:

- `pj-signal-server listening on TCP/UDP port 11999 and candidate exchange TCP port 12000`

---

## 3) Run network test on both machines

Use the same group on both machines:

```bash
GROUP=lan-test-1
```

Machine A:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-smoke.conf \
  --server-host 192.168.0.21 \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110
```

Machine B:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
./build-portaudio/pj-net-test \
  --config resources/config/local-tester-smoke.conf \
  --server-host 192.168.0.21 \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111
```

Success criteria:

- both commands exit `0`
- both print JSON with `network`, `probe`, `session_profile`

---

## 4) Start UI server on both machines

Machine A:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native/ui/peerjam-monitor
python3 -m http.server 8088
```

Machine B:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native/ui/peerjam-monitor
python3 -m http.server 8088
```

Open browser:

- Machine A browser: `http://127.0.0.1:8088/index.html?ts=1`
- Machine B browser: `http://127.0.0.1:8088/index.html?ts=1`

---

## 5) Run peer probe (audio + UI telemetry)

Use same group:

```bash
GROUP=lan-audio-1
```

Machine A:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
./build-portaudio/pj-peer-probe \
  --config resources/config/local-tester-smoke.conf \
  --server-host 192.168.0.21 \
  --group "$GROUP" \
  --user alice \
  --udp-port 12110 \
  --ui-telemetry-port 17070 \
  --ui-telemetry-interval-ms 100 \
  --audio-device-io \
  --audio-input-device -1 \
  --audio-output-device -1 \
  --tone-hz 0 \
  --frame-samples 256 \
  --source-buffer-ms 8 \
  --sink-buffer-ms 20 \
  --audio-seconds 120
```

Machine B:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
./build-portaudio/pj-peer-probe \
  --config resources/config/local-tester-smoke.conf \
  --server-host 192.168.0.21 \
  --group "$GROUP" \
  --user bob \
  --udp-port 12111 \
  --ui-telemetry-port 17071 \
  --ui-telemetry-interval-ms 100 \
  --audio-device-io \
  --audio-input-device -1 \
  --audio-output-device -1 \
  --tone-hz 0 \
  --frame-samples 256 \
  --source-buffer-ms 8 \
  --sink-buffer-ms 20 \
  --audio-seconds 120
```

---

## 6) Connect UI telemetry

Machine A browser:

- endpoint: `http://127.0.0.1:17070/events`
- click `Connect`
- click `Refresh Devices`
- choose input/output devices
- click `Apply Devices`

Machine B browser:

- endpoint: `http://127.0.0.1:17071/events`
- click `Connect`
- click `Refresh Devices`
- choose input/output devices
- click `Apply Devices`

Optional checks:

```bash
curl -s http://127.0.0.1:17070/devices
curl -s http://127.0.0.1:17071/devices
```

---

## 7) Troubleshooting

If a peer says `no remote peer joined the group`:

- ensure same `GROUP` value
- start second peer within 10-15 seconds

If UI device dropdown is empty:

- verify peer telemetry port is listening (`lsof -nP -iTCP:17070 -sTCP:LISTEN`)
- check OS microphone permission for terminal app
- use hard refresh in browser (`Cmd+Shift+R`)

If UDP bind fails:

```bash
lsof -nP -iUDP:12110
lsof -nP -iUDP:12111
kill <PID>
```

---

## 8) Cleanup

Machine A:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
pkill -f pj-peer-probe || true
pkill -f "python3 -m http.server 8088" || true
make server-stop
```

Machine B:

```bash
cd /Users/anepoti/workspace/experiments/audio-jam/peerjam-native
pkill -f pj-peer-probe || true
pkill -f "python3 -m http.server 8088" || true
```
