# PeerJam Native

C++ runtime for low-latency peer-to-peer music sessions over UDP.

## What This Repo Contains

- `pj-signal-server`: signaling + candidate exchange
- `pj-net-test`: network qualification and session profile output
- `pj-peer-probe`: full peer runtime (probe + optional audio)

Core goals:

- direct P2P media path by default
- measurable latency/jitter/loss and readiness (`ready` / `marginal` / `not_ready`)
- performance-first runtime behavior

## Governance

Project governance and architecture policy live in [`.specify/`](.specify):

- constitution: [`.specify/memory/constitution.md`](.specify/memory/constitution.md)
- architecture decisions: [`.specify/adr/README.md`](.specify/adr/README.md)

Any architectural or runtime policy change must align with the constitution.

## Quick Start

1. Build

```bash
cmake --fresh -S . -B build-portaudio -DPEERJAM_ENABLE_PORTAUDIO=ON
cmake --build build-portaudio -j 4
```

2. Start signaling server

```bash
./build-portaudio/pj-signal-server --config resources/config/signal-server.conf
```

3. Run network test on both peers (same `--group`, different `--user`)

```bash
./build-portaudio/pj-net-test --config resources/config/net-test.conf ...
```

4. Run peer runtime (optional audio)

```bash
./build-portaudio/pj-peer-probe --config resources/config/peer-probe.conf ...
```

Use the full consolidated runbook in [HOWTO.md](HOWTO.md).

## Config

Runtime configs are in [resources/config](resources/config):

- `signal-server.conf`
- `net-test.conf`
- `peer-probe.conf`
- local/two-machine examples for practical testing

## Notes

- AOO is vendored under `third_party/aoo` (self-contained build path).
- If you copied build artifacts across machines/OSes, clean and rebuild from scratch.
- For constrained/sandboxed environments, runbooks must explicitly mark commands that require elevated permissions.
