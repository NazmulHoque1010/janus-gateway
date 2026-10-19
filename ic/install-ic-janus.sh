#!/usr/bin/env bash
# =============================================================================
# install-ic-janus.sh: install the IC build of Janus (AudioCall + CustomVideoCall
# plugins with automatic LAN discovery) on a new server, e.g. PC3.
#
# Run it as your normal user (NOT with sudo). It asks for your sudo password
# when it needs it.
#
#   bash install-ic-janus.sh --name pc3
#
# Options:
#   --name NAME        server_name of this Janus (must be unique on the LAN). Default: the host name
#   --autostart yes|no start Janus at boot (yes, like PC1) or only by hand (no, like PC2). Default: no
#   --branch BRANCH    git branch to build. Default: ic-custom
#   --repo URL         git repository. Default: https://github.com/NazmulHoque1010/janus-gateway.git
#   --src DIR          where the source goes. Default: ~/janus-dev
#   --rtp-range A-B    WebRTC media port range. Default: 35000-40000
#   --ws-port PORT     Janus WebSocket port told to the apps. Default: 8188
#   --skip-deps        don't apt-install the build dependencies
#   --no-service       don't create/start the systemd service (only build, install and configure)
#   -h, --help         show this help
#
# What it does, in order (each step is printed as [n/12]):
#    1  checks: Ubuntu/Debian, sudo, options
#    2  network check: this PC's IP, and which IC Janus servers already answer on the LAN
#    3  refuses to continue if another server already uses the same --name
#    4  installs the build dependencies (apt)
#    5  clones the repository (or updates an existing clone)
#    6  builds: autogen.sh, configure, make (checks both IC plugins are built)
#    7  installs to /opt/janus (make install; "make configs" ONLY on the first install)
#    8  writes the two plugin configs (server_name + link_port, no IPs), backing up old ones
#    9  sets rtp_port_range in janus.jcfg
#   10  firewall rules, if ufw is active
#   11  systemd service, autostart yes/no, (re)start Janus
#   12  verifies: plugins initialized, other servers discovered, discovery answer from this PC
#
# Safe to run again: it updates the code, rebuilds, reinstalls and keeps the
# other .jcfg files.
# =============================================================================
set -Eeuo pipefail

# ---------------------------------------------------------------- defaults
SERVER_NAME="$(hostname -s)"
AUTOSTART="no"
BRANCH="ic-custom"
REPO="https://github.com/NazmulHoque1010/janus-gateway.git"
SRC="$HOME/janus-dev"
RTP_RANGE="35000-40000"
WS_PORT="8188"
SKIP_DEPS=0
NO_SERVICE=0
PREFIX="/opt/janus"
CONF="$PREFIX/etc/janus"
LAN_RULE_SOURCE=""          # filled in from this PC's IP/prefix (step 2)

STEP=0
TOTAL=12
CURRENT="starting"

# ---------------------------------------------------------------- helpers
c_blue=$'\e[1;34m'; c_green=$'\e[1;32m'; c_yellow=$'\e[1;33m'; c_red=$'\e[1;31m'; c_off=$'\e[0m'
step()  { STEP=$((STEP+1)); CURRENT="$*"; echo; echo "${c_blue}[$STEP/$TOTAL] $*${c_off}"; }
info()  { echo "      $*"; }
ok()    { echo "      ${c_green}OK${c_off} $*"; }
warn()  { echo "      ${c_yellow}WARNING${c_off} $*"; }
die()   { echo; echo "${c_red}ERROR in step $STEP ($CURRENT): $*${c_off}"; exit 1; }
trap 'echo; echo "${c_red}FAILED in step $STEP ($CURRENT): command \"$BASH_COMMAND\" (line $LINENO)${c_off}"; echo "Fix the problem and run the script again; finished steps are safe to repeat."' ERR

usage() { sed -n '3,/^# ====/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0; }

if [[ $EUID -eq 0 ]]; then SUDO=""; else SUDO="sudo"; fi

# Ask the LAN which IC Janus servers exist (same packet the app sends)
discover() {
python3 - <<'PY'
import socket, time, json, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.settimeout(0.3)
import subprocess, re
targets = ['255.255.255.255']
try:   # each interface's own broadcast address too (works without a default route)
    out = subprocess.run(['ip', '-4', '-o', 'addr', 'show', 'scope', 'global'], capture_output=True, text=True).stdout
    targets += re.findall(r'brd (\d+\.\d+\.\d+\.\d+)', out)
except Exception:
    pass
for _ in range(2):
    for t in dict.fromkeys(targets):
        try:
            s.sendto(b'{"type":"discover"}', (t, 7088))
        except OSError:
            pass
    time.sleep(0.2)
end = time.time() + 1.2
seen = {}
while time.time() < end:
    try:
        data, addr = s.recvfrom(2000)
        m = json.loads(data.decode())
        if m.get('type') == 'here':
            seen[m.get('server')] = (addr[0], m.get('users'))
    except Exception:
        pass
for name, (ip, users) in sorted(seen.items()):
    print(f"{name} {ip} {users}")
PY
}

# ---------------------------------------------------------------- options
while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)       SERVER_NAME="$2"; shift 2 ;;
    --autostart)  AUTOSTART="$2"; shift 2 ;;
    --branch)     BRANCH="$2"; shift 2 ;;
    --repo)       REPO="$2"; shift 2 ;;
    --src)        SRC="$2"; shift 2 ;;
    --rtp-range)  RTP_RANGE="$2"; shift 2 ;;
    --ws-port)    WS_PORT="$2"; shift 2 ;;
    --skip-deps)  SKIP_DEPS=1; shift ;;
    --no-service) NO_SERVICE=1; shift ;;
    -h|--help)    usage ;;
    *) echo "Unknown option: $1 (see --help)"; exit 1 ;;
  esac
done

echo "${c_blue}IC Janus installer${c_off}: server_name='$SERVER_NAME', branch '$BRANCH', source '$SRC', autostart '$AUTOSTART'"

# =============================================================== 1. checks
step "Checks"
info "OS: $( (. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}") )"
command -v apt-get >/dev/null || die "this script needs an Ubuntu/Debian system (apt-get not found)"
[[ "$SERVER_NAME" =~ ^[A-Za-z0-9_.-]+$ ]] || die "--name may only contain letters, digits, '.', '_' and '-'"
[[ "$AUTOSTART" == "yes" || "$AUTOSTART" == "no" ]] || die "--autostart must be yes or no"
[[ "$RTP_RANGE" =~ ^[0-9]+-[0-9]+$ ]] || die "--rtp-range must look like 35000-40000"
if [[ -n "$SUDO" ]]; then
  info "Asking for your sudo password once (needed for apt, /opt/janus, systemd, ufw)..."
  sudo -v || die "sudo is required"
  # keep sudo alive while the script runs
  ( while true; do sudo -n true; sleep 50; kill -0 "$$" 2>/dev/null || exit; done ) 2>/dev/null &
fi
command -v python3 >/dev/null || { info "Installing python3 (needed for the network checks)"; $SUDO apt-get install -y python3; }
ok "server_name '$SERVER_NAME'"

# =============================================================== 2. network
step "Network check"
IFACE_LINE="$(ip -4 -o addr show scope global | head -1 || true)"
[[ -n "$IFACE_LINE" ]] || die "this PC has no IPv4 address on the network"
MY_CIDR="$(awk '{print $4}' <<<"$IFACE_LINE")"
MY_IP="${MY_CIDR%/*}"
LAN_RULE_SOURCE="$(python3 -c "import ipaddress;print(ipaddress.ip_interface('$MY_CIDR').network)")"
info "This PC: $MY_IP (network $LAN_RULE_SOURCE, interface $(awk '{print $2}' <<<"$IFACE_LINE"))"
SERVERS="$(discover)"
if [[ -n "$SERVERS" ]]; then
  info "IC Janus servers answering on the LAN:"
  while read -r n ip users; do info "  - $n at $ip ($users users)"; done <<<"$SERVERS"
else
  warn "no IC Janus server answered on the LAN (fine if this is the first one; otherwise check the network)"
fi

# =============================================================== 3. unique name
step "Unique server_name"
if [[ -n "$SERVERS" ]] && awk -v n="$SERVER_NAME" -v me="$MY_IP" '$1==n && $2!=me {found=1} END{exit !found}' <<<"$SERVERS"; then
  die "server_name '$SERVER_NAME' is already used by $(awk -v n="$SERVER_NAME" '$1==n{print $2}' <<<"$SERVERS"). Choose another one with --name"
fi
ok "'$SERVER_NAME' is not used by another server"

# =============================================================== 4. dependencies
step "Build dependencies"
if [[ $SKIP_DEPS -eq 1 ]]; then
  info "skipped (--skip-deps)"
else
  PKGS=(git build-essential autoconf automake libtool pkg-config gengetopt
        libglib2.0-dev libconfig-dev libnice-dev libjansson-dev libssl-dev libsrtp2-dev
        libmicrohttpd-dev libwebsockets-dev libopus-dev libogg-dev libcurl4-openssl-dev zlib1g-dev)
  # A FreeSWITCH that built its own sofia-sip must keep using that one
  if pkg-config --exists sofia-sip-ua 2>/dev/null; then
    info "sofia-sip $(pkg-config --modversion sofia-sip-ua) already present (e.g. from FreeSWITCH): not installing Ubuntu's copy"
  else
    PKGS+=(libsofia-sip-ua-glib-dev)
  fi
  $SUDO apt-get update
  $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y "${PKGS[@]}"
  ok "dependencies installed"
fi

# =============================================================== 5. source
step "Source code ($REPO, branch $BRANCH)"
if [[ -d "$SRC/.git" ]]; then
  info "existing clone in $SRC: updating"
  git -C "$SRC" fetch origin
  git -C "$SRC" checkout "$BRANCH"
  git -C "$SRC" pull --ff-only origin "$BRANCH"
else
  git clone --branch "$BRANCH" "$REPO" "$SRC"
fi
info "at commit: $(git -C "$SRC" log --oneline -1)"
ok "source ready"

# =============================================================== 6. build
step "Build (autogen, configure, make)"
cd "$SRC"
./autogen.sh > /tmp/ic-janus-autogen.log 2>&1 || { tail -20 /tmp/ic-janus-autogen.log; die "autogen.sh failed (log: /tmp/ic-janus-autogen.log)"; }
./configure --prefix="$PREFIX" > /tmp/ic-janus-configure.log 2>&1 || { tail -30 /tmp/ic-janus-configure.log; die "configure failed (log: /tmp/ic-janus-configure.log)"; }
grep -E "WebSockets|Audio Call \(IC\)|Custom Video Call" /tmp/ic-janus-configure.log | sed 's/^/      /' || true
grep -q "Audio Call (IC):       yes" /tmp/ic-janus-configure.log || die "configure says the AudioCall plugin will not be built (see /tmp/ic-janus-configure.log)"
info "compiling with $(nproc) cores (takes a few minutes)..."
make -j"$(nproc)" > /tmp/ic-janus-make.log 2>&1 || { grep -E "error" /tmp/ic-janus-make.log | head -20; die "make failed (log: /tmp/ic-janus-make.log)"; }
for la in src/plugins/libjanus_audiocall.la src/plugins/libjanus_customvideocall.la; do
  [[ -f "$la" ]] || die "$la was not built"
done
ok "built: $(./src/janus --version 2>/dev/null | head -1)"

# =============================================================== 7. install
step "Install to $PREFIX"
FIRST_INSTALL=0
[[ -f "$CONF/janus.jcfg" ]] || FIRST_INSTALL=1
$SUDO make install > /tmp/ic-janus-install.log 2>&1 || die "make install failed (log: /tmp/ic-janus-install.log)"
if [[ $FIRST_INSTALL -eq 1 ]]; then
  info "first install: writing the default configs (make configs)"
  $SUDO make configs > /dev/null
else
  info "existing configs in $CONF kept (make configs NOT run)"
fi
for so in libjanus_audiocall.so libjanus_customvideocall.so; do
  [[ -e "$PREFIX/lib/janus/plugins/$so" ]] || die "$so was not installed"
  info "$PREFIX/lib/janus/plugins/$so"
done
ok "installed"

# =============================================================== 8. plugin configs
step "Plugin configs (server_name '$SERVER_NAME', no peer IPs)"
STAMP="$(date +%Y%m%d-%H%M%S)"
write_plugin_cfg() {   # $1 = plugin package, $2 = link port
  local f="$CONF/$1.jcfg"
  [[ -f "$f" ]] && $SUDO cp "$f" "$f.bak-$STAMP" && info "backup: $f.bak-$STAMP"
  $SUDO tee "$f" > /dev/null <<EOF
general: {
	server_name = "$SERVER_NAME"
	link_port = $2
	ws_port = $WS_PORT
}
EOF
  info "$f:"; sed 's/^/        /' "$f"
}
write_plugin_cfg janus.plugin.audiocall 7088
write_plugin_cfg janus.plugin.customvideocall 7089
ok "plugin configs written"

# =============================================================== 9. janus.jcfg
step "Media port range in janus.jcfg"
$SUDO sed -i "s|^\s*#\?\s*rtp_port_range = .*|\trtp_port_range = \"$RTP_RANGE\"|" "$CONF/janus.jcfg"
grep -n "rtp_port_range = " "$CONF/janus.jcfg" | sed 's/^/      /'
grep -q "rtp_port_range = \"$RTP_RANGE\"" "$CONF/janus.jcfg" || die "could not set rtp_port_range in $CONF/janus.jcfg"
ok "rtp_port_range = $RTP_RANGE"

# =============================================================== 10. firewall
step "Firewall"
if command -v ufw >/dev/null && $SUDO ufw status | grep -q "Status: active"; then
  $SUDO ufw allow "$WS_PORT"/tcp
  $SUDO ufw allow from "$LAN_RULE_SOURCE" to any port 7088 proto udp
  $SUDO ufw allow from "$LAN_RULE_SOURCE" to any port 7089 proto udp
  $SUDO ufw allow "${RTP_RANGE/-/:}"/udp
  ok "ufw: $WS_PORT/tcp, 7088+7089/udp from $LAN_RULE_SOURCE, ${RTP_RANGE/-/:}/udp"
else
  info "ufw not active: nothing to do"
fi

# =============================================================== 11. service
step "Systemd service"
if [[ $NO_SERVICE -eq 1 ]]; then
  info "skipped (--no-service). Start Janus yourself: $PREFIX/bin/janus"
else
  $SUDO tee /etc/systemd/system/janus.service > /dev/null <<'EOF'
[Unit]
Description=Janus WebRTC Server (IC build)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/opt/janus/bin/janus
Restart=on-failure
RestartSec=5
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF
  $SUDO systemctl daemon-reload
  if [[ "$AUTOSTART" == "yes" ]]; then $SUDO systemctl enable janus; else $SUDO systemctl disable janus 2>/dev/null || true; fi
  info "autostart at boot: $AUTOSTART"
  $SUDO systemctl restart janus
  sleep 6
  systemctl is-active --quiet janus || { $SUDO journalctl -u janus -n 30 --no-pager; die "Janus did not start"; }
  ok "Janus is running"
fi

# =============================================================== 12. verify
step "Verification"
if [[ $NO_SERVICE -eq 1 ]]; then
  info "service not managed by this script: verify after starting Janus"
else
  LOG="$($SUDO journalctl -u janus --since "2 minutes ago" --no-pager 2>/dev/null || true)"
  grep -E "initialized! \(server|Link thread started|Discovered server" <<<"$LOG" | sed 's/^.*janus\[[0-9]*\]: /      /' || true
  grep -q "AudioCall plugin initialized! (server '$SERVER_NAME')" <<<"$LOG" || die "AudioCall plugin did not initialize (see: sudo journalctl -u janus)"
  grep -q "CustomVideoCall plugin initialized! (server '$SERVER_NAME')" <<<"$LOG" || die "CustomVideoCall plugin did not initialize"
  ss -ltn | grep -q ":$WS_PORT " && ok "WebSocket port $WS_PORT is listening" || warn "nothing listening on $WS_PORT"
  if [[ -n "$SERVERS" ]]; then
    grep -q "Discovered server" <<<"$LOG" && ok "other servers discovered" || warn "no other server discovered yet (check the firewall / network, then: sudo journalctl -u janus | grep Discovered)"
  fi
  NOW="$(discover)"
  info "Servers answering discovery now:"
  while read -r n ip users; do [[ -n "$n" ]] && info "  - $n at $ip ($users users)"; done <<<"$NOW"
  grep -q "^$SERVER_NAME " <<<"$NOW" && ok "'$SERVER_NAME' answers discovery: apps can be placed here" || warn "'$SERVER_NAME' does not answer discovery yet"
fi

echo
echo "${c_green}Done.${c_off} '$SERVER_NAME' is installed in $PREFIX."
echo "  Logs:     sudo journalctl -u janus -f"
echo "  On the other servers you should see: Discovered server '$SERVER_NAME' at $MY_IP:7088 (and :7089)"
echo "  Apps need no change: the next app that starts can be placed on '$SERVER_NAME'."
