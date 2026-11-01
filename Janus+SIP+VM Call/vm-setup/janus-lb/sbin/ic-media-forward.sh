#!/bin/sh
# ic-media-forward.sh - janus-lb forwards FreeSWITCH RTP between VM1 and VM2.
# Each FreeSWITCH advertises janus-lb (10.0.0.28) as its media address on its own
# port range; janus-lb forwards that range to the owning VM and rewrites the sender
# to itself, so both FreeSWITCH servers only ever exchange media with janus-lb.
set -e
VM1=10.0.0.18; VM2=10.0.0.25; LB=10.0.0.28; IF=ens3
VM1_RTP=20000:24999      # FreeSWITCH media ports on VM1
VM2_RTP=25000:29999      # FreeSWITCH media ports on VM2

sysctl -q -w net.ipv4.ip_forward=1

# Own chains, created if missing and emptied, so re-running is safe
iptables -t nat -N IC_PRE  2>/dev/null || true; iptables -t nat -F IC_PRE
iptables -t nat -N IC_POST 2>/dev/null || true; iptables -t nat -F IC_POST
iptables        -N IC_FWD  2>/dev/null || true; iptables        -F IC_FWD

# Media for VM1 (sent by VM2) and media for VM2 (sent by VM1)
iptables -t nat -A IC_PRE  -i $IF -p udp -s $VM2 -d $LB --dport $VM1_RTP -j DNAT --to-destination $VM1
iptables -t nat -A IC_PRE  -i $IF -p udp -s $VM1 -d $LB --dport $VM2_RTP -j DNAT --to-destination $VM2
iptables -t nat -A IC_POST -o $IF -p udp -d $VM1 --dport $VM1_RTP -j SNAT --to-source $LB
iptables -t nat -A IC_POST -o $IF -p udp -d $VM2 --dport $VM2_RTP -j SNAT --to-source $LB

# Allowed forwarding (these two rules' counters = media packets/bytes towards each VM)
iptables -A IC_FWD -i $IF -o $IF -p udp -s $VM2 -d $VM1 --dport $VM1_RTP -j ACCEPT
iptables -A IC_FWD -i $IF -o $IF -p udp -s $VM1 -d $VM2 --dport $VM2_RTP -j ACCEPT

# Hook the chains in once, and forward nothing else
iptables -t nat -C PREROUTING  -j IC_PRE  2>/dev/null || iptables -t nat -I PREROUTING  -j IC_PRE
iptables -t nat -C POSTROUTING -j IC_POST 2>/dev/null || iptables -t nat -I POSTROUTING -j IC_POST
iptables        -C FORWARD     -j IC_FWD  2>/dev/null || iptables        -I FORWARD     -j IC_FWD
iptables -P FORWARD DROP
echo "ic-media-forward: rules loaded"
