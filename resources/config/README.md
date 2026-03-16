# Configuration Files

This directory stores the runtime configuration files for:

- `pj-signal-server`
- `pj-net-test`
- `pj-peer-probe`

Each application loads its matching file by default:

- `resources/config/signal-server.conf`
- `resources/config/net-test.conf`
- `resources/config/peer-probe.conf`

Additional ready-to-edit examples for a local-server/remote-client setup:

- `resources/config/local-server-public.conf`
- `resources/config/local-tester-public.conf`
- `resources/config/local-tester-smoke.conf`
- `resources/config/remote-tester-public.conf`

Those example files default to:

- signaling port `11999`
- candidate exchange port `12000`

Override the default file with:

```bash
--config /path/to/file.conf
```

The format is a simple `key = value` text file.

## Local tester note

For first-time local `pj-net-test` runs, set:

```ini
profile.in =
```

in `resources/config/local-tester-public.conf` unless you already generated a profile file.

`local-tester-smoke.conf` is a relaxed local-only profile for reproducibility/smoke checks.
Do not use its recommendation thresholds for cross-site readiness decisions.
