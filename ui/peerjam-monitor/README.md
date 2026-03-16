# PeerJam Monitor UI

Simple browser UI for:

- live microphone capture waveform
- capture gain and monitor volume controls
- telemetry panel (latency, jitter, bandwidth) with ms highlighting
- predictor panel (risk, requested mitigation, applied mitigation, apply status)
- parsing `pj-peer-probe` JSON output into telemetry cards

## Run

From the repository root:

```bash
cd ui/peerjam-monitor
python3 -m http.server 8088
```

Open:

`http://127.0.0.1:8088`

## Usage with peer-to-peer run

1. Start your peers with `pj-peer-probe`.
2. Start a peer with realtime UI telemetry enabled:

```bash
./build-portaudio/pj-peer-probe \
  --server-host 127.0.0.1 \
  --group voice-test \
  --user alice \
  --udp-port 12000 \
  --audio-seconds 30 \
  --audio-device-io \
  --ui-telemetry-port 17070 \
  --ui-telemetry-interval-ms 100
```

3. In the UI, click `Connect` for `http://127.0.0.1:17070/events`.
4. Click `Refresh Devices`, choose `C++ Input Device` and `C++ Output Device`, then `Apply Devices`.
5. Start the remote peer in the same group and run audio.
6. The local capture waveform and received waveform now update in realtime from the C++ process.
7. You can still paste final JSON into `Parse Report` for post-run validation.
8. The telemetry panel highlights:
   - latency: warm at `>= 12 ms`, hot at `>= 30 ms`
   - jitter: warm at `>= 2.5 ms`, hot at `>= 5 ms`

Latency over `30 ms` is marked as a budget breach.

Bandwidth is shown in `kbps` and estimated from codec:

- PCM: `3072 kbps` (48 kHz, stereo, float32 equivalent)
- Opus: `96 kbps` nominal monitor estimate

Predictor apply status labels:

- `no_action`
- `applied`
- `shadow_mode`
- `cooldown`
- `codec_locked`
- `no_op`
