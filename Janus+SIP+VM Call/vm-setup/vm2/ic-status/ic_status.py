#!/usr/bin/env python3
"""ic_status.py - IC server status service (one per Janus + FreeSWITCH VM).

Answers read-only JSON over HTTP so the IC app can choose a server and a name:

  GET /status  -> this server plus every peer: addresses, registered users, load
  GET /local   -> this server only (what peers ask each other for)
  GET /health  -> "ok"

Users come from the local FreeSWITCH (event socket on 127.0.0.1) and are the
names registered on the configured SIP profile and domain. A name registered from
several contacts (e.g. right after an app reopen) counts once.

Standard library only. Config: JSON file (default /etc/ic-status.json).
"""
import argparse
import json
import re
import socket
import sys
import threading
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "1.1"
CACHE_SECONDS = 2.0
PEER_TIMEOUT = 1.5
ESL_TIMEOUT = 3.0

CFG = {}
_cache = {"t": 0.0, "local": None}
_cache_lock = threading.Lock()


def _esl_read_message(f):
    """Read one ESL message: headers up to a blank line, then Content-Length body."""
    headers = {}
    while True:
        line = f.readline()
        if not line:
            raise ConnectionError("event socket closed")
        line = line.decode("utf-8", "replace").rstrip("\r\n")
        if line == "":
            if headers:
                break
            continue
        k, _, v = line.partition(":")
        headers[k.strip().lower()] = v.strip()
    body = b""
    n = int(headers.get("content-length", "0") or 0)
    if n:
        body = f.read(n)
    return headers, body.decode("utf-8", "replace")


def esl_api(command):
    """Run one FreeSWITCH API command through the event socket and return its text."""
    e = CFG["esl"]
    with socket.create_connection((e["host"], int(e["port"])), timeout=ESL_TIMEOUT) as s:
        f = s.makefile("rb")
        h, _ = _esl_read_message(f)
        if h.get("content-type") != "auth/request":
            raise ConnectionError("unexpected event socket greeting")
        s.sendall(("auth %s\n\n" % e["password"]).encode())
        h, _ = _esl_read_message(f)
        if not h.get("reply-text", "").startswith("+OK"):
            raise PermissionError("event socket auth failed")
        s.sendall(("api %s\n\n" % command).encode())
        h, body = _esl_read_message(f)
        try:
            s.sendall(b"exit\n\n")
        except OSError:
            pass
        return body


USER_RE = re.compile(r"<user>\s*([^<@\s]+)@([^<\s]+)\s*</user>")


def registered_users():
    """Sorted unique names registered in our domain on any of our SIP profiles
    (app users via Janus on "internal", SIP phones such as MizuDroid on "phones")."""
    names = set()
    for profile in CFG["sip_profiles"]:
        xml = esl_api("sofia xmlstatus profile %s reg" % profile)
        names |= {u for u, dom in USER_RE.findall(xml) if dom == CFG["domain"]}
    return sorted(names)


def local_entry():
    """This server's entry, cached for a couple of seconds."""
    with _cache_lock:
        if _cache["local"] is not None and time.time() - _cache["t"] < CACHE_SECONDS:
            return _cache["local"]
    entry = {
        "server": CFG["server"],
        "public_host": CFG["public_host"],
        "ws_port": CFG["ws_port"],
        "sip_proxy": CFG["sip_proxy"],
        "domain": CFG["domain"],
        "online": True,
    }
    try:
        users = registered_users()
        entry.update(users=users, load=len(users), sip_ok=True)
    except Exception as ex:  # FreeSWITCH down: still report the server, but not usable
        entry.update(users=[], load=None, sip_ok=False, error=str(ex))
    with _cache_lock:
        _cache.update(t=time.time(), local=entry)
    return entry


def fetch_peer(url):
    try:
        with urllib.request.urlopen(url.rstrip("/") + "/local", timeout=PEER_TIMEOUT) as r:
            entry = json.loads(r.read().decode())
            entry["online"] = True
            return entry
    except Exception as ex:
        return {"server": None, "peer_url": url, "online": False, "error": str(ex)}


def full_status():
    me = local_entry()
    peers = CFG.get("peers", [])
    with ThreadPoolExecutor(max_workers=max(1, len(peers))) as pool:
        others = list(pool.map(fetch_peer, peers))
    return {
        "server": CFG["server"],
        "domain": CFG["domain"],
        "version": VERSION,
        "time": int(time.time()),
        "servers": [me] + others,
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "ic-status/" + VERSION

    def _send(self, code, obj):
        data = (json.dumps(obj, separators=(",", ":")) + "\n").encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        if path in ("/", "/status"):
            self._send(200, full_status())
        elif path == "/local":
            self._send(200, local_entry())
        elif path == "/health":
            self._send(200, {"ok": True, "server": CFG["server"]})
        else:
            self._send(404, {"error": "not found"})

    def log_message(self, fmt, *args):  # keep the journal quiet
        pass


def load_config(path):
    with open(path) as f:
        cfg = json.load(f)
    cfg.setdefault("listen", "0.0.0.0")
    cfg.setdefault("port", 8190)
    cfg.setdefault("ws_port", 8188)
    cfg.setdefault("sip_proxy", "sip:127.0.0.1:5060")
    cfg.setdefault("domain", "ic.local")
    profiles = cfg.get("sip_profiles") or cfg.get("sip_profile") or "internal"
    cfg["sip_profiles"] = [profiles] if isinstance(profiles, str) else list(profiles)
    cfg.setdefault("peers", [])
    cfg.setdefault("esl", {})
    cfg["esl"].setdefault("host", "127.0.0.1")
    cfg["esl"].setdefault("port", 8021)
    cfg["esl"].setdefault("password", "ClueCon")
    for key in ("server", "public_host"):
        if not cfg.get(key):
            sys.exit("config: '%s' is required" % key)
    return cfg


def main():
    ap = argparse.ArgumentParser(description="IC server status service")
    ap.add_argument("--config", default="/etc/ic-status.json")
    args = ap.parse_args()
    CFG.update(load_config(args.config))
    httpd = ThreadingHTTPServer((CFG["listen"], int(CFG["port"])), Handler)
    print("ic-status %s: server=%s listening on %s:%s, profiles=%s, peers=%s"
          % (VERSION, CFG["server"], CFG["listen"], CFG["port"], CFG["sip_profiles"],
             CFG["peers"]), flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
