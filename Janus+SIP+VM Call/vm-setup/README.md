# vm-setup: two-VM Janus + FreeSWITCH (SIP) with janus-lb carrying cross-VM media

The configuration of the running cloud setup (Nov 2026). Only files that were
created or changed are here, laid out by machine. (`../captured/` is the older
PC1 billing server and is unrelated to this.)

| Machine | Private IP | Role |
|---|---|---|
| vm1 (janus-vm-01) | 10.0.0.18 | Janus 1.4 (IC build, SIP plugin) + FreeSWITCH 1.10.10, FreeSWITCH RTP 20000-24999 |
| vm2 (janus-vm-02) | 10.0.0.25 | same as vm1, FreeSWITCH RTP 25000-29999 |
| janus-lb | 10.0.0.28 | forwards FreeSWITCH media between the VMs (kernel DNAT/SNAT), plus the old app's media_relay.py |

## Where each file goes

| In this folder | On the server |
|---|---|
| `vmX/freeswitch/conf/...` | `/usr/local/freeswitch/conf/...` (same relative path) |
| `vmX/freeswitch/scripts/ic_directory.lua` | `/usr/local/freeswitch/scripts/` |
| `vmX/janus/janus.plugin.sip.jcfg` | `/opt/janus/etc/janus/` |
| `vmX/ic-status/ic_status.py`, `ic-status.json` | `/opt/ic-status/ic_status.py`, `/etc/ic-status.json` |
| `vmX/firewall/ic-peer-firewall.sh` | `/usr/local/sbin/` |
| `vmX/systemd/*.service` | `/etc/systemd/system/` |
| `janus-lb/sbin/ic-media-forward.sh` | `/usr/local/sbin/` |
| `janus-lb/systemd/*.service` | `/etc/systemd/system/` |
| `janus-lb/sysctl/60-ic-media-forward.conf` | `/etc/sysctl.d/` |

## What the pieces do

- FreeSWITCH profiles: `internal` (127.0.0.1:5060, only the local Janus), `phones`
  (private IP:5090, public IP advertised, for MizuDroid and other SIP phones),
  `peer` (private IP:5070, only the other VM, media advertised at janus-lb).
- `ic_directory.lua`: any valid name in `ic.local` registers with password 1234 via
  `internal`; on `phones` only names with a user file (`directory/default/mizu1.xml`, ...).
- Dialplan: `00_ic_users.xml` rings a name locally, otherwise sends the call to the
  other VM over `peer`; `from_peer.xml` only rings local users.
- `ic_status.py` (port 8190): which servers exist, their load and who is online,
  read from FreeSWITCH (`internal` + `phones`); the app uses it to pick a server.
- `ic-peer-firewall.sh`: port 5070 only from the other VM.
- `ic-media-forward.sh` (janus-lb): 20000-24999 -> vm1, 25000-29999 -> vm2, sender
  rewritten to janus-lb, nothing else forwarded.

## Placeholders and lab defaults

- `__IC_PUBLIC_IP__`: the VM's public IP (vars.xml `ic_public_ip`, ic-status.json `public_host`).
- `__IC_SECRET_DEFAULT_PASSWORD__`: any random string (vars.xml).
- Lab defaults left in place: password `1234` for app users and for `mizu1`/`mizu2`,
  and FreeSWITCH's stock event-socket password `ClueCon` (listens on 127.0.0.1 only).
  Change `mizu*.xml` passwords before exposing port 5090 more widely.

## The app

`ic/sipcall_app` (Flutter, package `com.ic.sipcall`). Build with
`--dart-define=IC_SEEDS=<vm1 public IP>,<vm2 public IP>`.
