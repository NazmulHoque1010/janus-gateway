# Janus+SIP+VM Call: one-command IC server setup

This folder sets up a complete IC calling server on a new Ubuntu machine (a PC or a cloud VM):

- **Janus, IC build** (branch `ic-custom`):
  - the AudioCall + CustomVideoCall plugins
  - server discovery, `public_host`, and the optional media relay
  - the **SIP plugin** (it builds with gcc 9 as well)
- **FreeSWITCH**, optional:
  - built from source exactly like PC1/PC2 (v1.10.12, without mod_signalwire and mod_av)
  - with **our configuration**: dialplan, Lua billing scripts, the xml_curl PHP service and custom sounds
- **The media relay** (`media_relay.py` at the repository root), optional, as a systemd service.

Anything already on the machine (Janus, FreeSWITCH, their systemd units, nginx) is **backed up first**, to `~/ic-server-backup-<time>.tgz`. Old installs are **moved aside, never deleted**.

| File | What it is |
|---|---|
| `install-ic-server.sh` | the installer, which also has the `--export` mode |
| `captured/` | the configuration of a working server, written by `--export`. Passwords are replaced by `__IC_SECRET_<NAME>__`, and that server's own IPs by `__IC_LOCAL_IP__` / `__IC_PUBLIC_IP__` |
| `captured/PLACEHOLDERS.txt` | which passwords are needed, and other servers' IPs worth checking |

## 1. Capture a working server (once, then whenever its configuration changes)

On the server whose setup should be copied, with this repository cloned in `~/janus-dev`:

```bash
cd ~/janus-dev
bash "Janus+SIP+VM Call/install-ic-server.sh" --export                 # Janus + FreeSWITCH configs
# optional: also the billing database SCHEMA (tables only, no data, nothing is changed):
bash "Janus+SIP+VM Call/install-ic-server.sh" --export --db-user fsuser
cat "Janus+SIP+VM Call/captured/PLACEHOLDERS.txt"
grep -rIn -i -E 'password|secret' "Janus+SIP+VM Call/captured" | grep -v __IC_SECRET_ | head -20   # review!
git add "Janus+SIP+VM Call" && git commit -m "Captured IC server configuration" && git push
```

**Never commit a real password.** If the review `grep` shows one, replace it by hand with `__IC_SECRET_<NAME>__`.

## 2. Install a new server

```bash
git clone --branch ic-custom https://github.com/NazmulHoque1010/janus-gateway.git ~/janus-dev
bash ~/janus-dev/"Janus+SIP+VM Call"/install-ic-server.sh --dry-run    # look only: prints the plan
bash ~/janus-dev/"Janus+SIP+VM Call"/install-ic-server.sh              # plan, confirm, passwords, install
```

| Situation | Add |
|---|---|
| FreeSWITCH is not on the machine yet and is needed | `--freeswitch install` (the build takes 30–60 min) |
| FreeSWITCH is already installed | nothing: `auto` deploys the captured configuration onto it |
| Janus only, leave FreeSWITCH alone | `--freeswitch skip` (it is still backed up) |
| LAN only (no public IP) | `--public-ip none` |
| No SIP plugin | `--sip no` |
| Start at boot | `--autostart yes` |
| Unattended | `--yes --secrets ~/ic-secrets.txt` (one `NAME=value` per line; keep this file **out of git**, `chmod 600`) |
| Media through a relay | `--media-relay <relay IP>:7090` |
| A relay-only machine (e.g. the load balancer) | `--media-relay-only` |
| Also run the relay on this server | `--install-media-relay` |
| Create the billing DB from the captured schema (local MySQL, only if it doesn't exist) | `--create-db` |

Run it as your **normal user** (not with `sudo`). The log goes to `~/ic-server-install-<time>.log`. If a step fails, the script stops, prints which step and command failed, and prints the commands that undo what it had changed so far. Fix the cause and run it again: running it again is safe, and also updates an existing IC server.

## What it never does

- remove packages or delete old installs
- upgrade installed packages (unless `--allow-upgrades`)
- change an existing database
- open the link or SIP ports to anything but the local network

## Status

- The export, the Janus build with SIP, deploying the configuration (passwords filled in), the PHP service, the media relay and the relay-only mode were tested on Ubuntu 24.04. The systemd services were simulated in that test.
- The FreeSWITCH **source build** (`--freeswitch install`) runs the same commands as `freeswitch-setup-documentation.md`, but was not re-run inside the test. Try it on a test machine first.
