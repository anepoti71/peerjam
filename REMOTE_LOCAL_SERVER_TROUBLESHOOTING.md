# Local Server, Remote Client Troubleshooting

This checklist is for the setup where:

- your local machine runs `pj-signal-server`
- one local client/tester also runs on that same machine or local network
- one remote machine connects over the internet

## 1. Public Reachability

The remote machine must reach your local machine using your public IP or DNS name.

Check:

- you know your public IP
- your ISP does not block inbound hosting
- your router forwards the required ports to your local machine

Required forwarded ports by default:

- `11999` for signaling
- `12000` for candidate exchange

## 2. Local Firewall

On the local server machine, allow inbound traffic for:

- TCP `11999`
- UDP `11999`
- TCP `12000`

If the firewall blocks these, the remote machine may fail to join even if port forwarding is correct.

## 3. Router / NAT

If the remote machine can reach the signaling server but no direct peer path is established:

- verify port forwarding is correct
- verify there is not double NAT
- verify your router is not doing symmetric NAT that breaks hole punching badly
- try enabling STUN in both client configs

## 4. Use Public IP On The Local Client Too

When the local machine also runs `pj-net-test` or `pj-peer-probe`, do not use `127.0.0.1` as `peer.server_host`.

Use your public IP or DNS name in:

- [local-tester-public.conf](resources/config/local-tester-public.conf)

This ensures both sides follow the same real internet signaling path.

## 5. Logging For Diagnosis

Temporarily enable logging:

```text
logging.enabled = true
logging.level = info
logging.sink = stderr
```

Use `debug` only when needed, because it adds more overhead.

For production-like performance runs, switch back to:

```text
logging.enabled = false
logging.level = off
```

## 6. STUN

If direct connectivity is unreliable, set:

- `peer.stun_host`
- `peer.stun_port`

in both the local and remote client config files.

## 7. Quick Failure Map

If the remote machine cannot join the group:

- check public IP
- check port forwarding
- check local firewall
- check the server is running

If both sides join but tester says no direct path:

- enable STUN
- verify UDP is not blocked
- check for strict NAT or double NAT

If the tester works but music performance is poor:

- repeat the test at several times of day
- prefer wired internet
- reduce background traffic
- check p95 one-way latency and p95 jitter

If local testing works but remote testing fails:

- the issue is almost always public reachability or NAT behavior, not the internal app logic
