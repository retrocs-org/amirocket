#!/usr/bin/env python3
"""
Mock Rocket.Chat REST server for AmiRocket tests.

Implements the subset of the REST API AmiRocket uses, with the response
shapes of Rocket.Chat 6.x-8.x, plus /mock/* hooks the test driver uses to
inject messages from other users and to inspect connection statistics.

  python3 mock_rocketchat.py --port 0 [--tls cert.pem key.pem] [--close-every N]

Prints "PORT <n>" on stdout once listening.
"""
import argparse
import datetime as dt
import json
import socket
import ssl
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

LOCK = threading.Lock()
STATS = {"connections": 0, "requests": 0, "dropped": 0}


def now_ms():
    return int(time.time() * 1000)


def iso(ms):
    d = dt.datetime.fromtimestamp(ms / 1000, tz=dt.timezone.utc)
    return d.strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % (ms % 1000)


def parse_iso(s):
    s = s.replace("Z", "+00:00")
    return int(dt.datetime.fromisoformat(s).timestamp() * 1000)


USERS = {
    "tommy": {"_id": "uid-tommy", "username": "tommy", "name": "Tommy F", "password": "secret", "totp": None},
    "secure": {"_id": "uid-secure", "username": "secure", "name": "Secure User", "password": "secret", "totp": "123456"},
    "alice": {"_id": "uid-alice", "username": "alice", "name": "Alice", "password": "x", "totp": None},
}
TOKENS = {}   # token -> username

T0 = now_ms() - 3600_000
ROOMS = {
    "GENERAL": {"t": "c", "name": "general", "fname": "general"},
    "devroom": {"t": "p", "name": "amiga-dev", "fname": "Amiga Dev"},
    "uid-aliceuid-tommy": {"t": "d", "name": "alice", "fname": "Alice"},
    "emptyroom": {"t": "c", "name": "empty", "fname": None},
    "atroom": {"t": "p", "name": "@zeta-group", "fname": None},
}
SUBS = {}       # rid -> subscription dict (single user model: tommy/secure share)
MESSAGES = {}   # rid -> list of message dicts
DELETED = {}    # rid -> list of (id, deleted_ms)
REMOVED_SUBS = []   # (rid, deleted_ms)


def make_msg(rid, username, text, ts=None, **extra):
    ts = ts or now_ms()
    u = USERS[username]
    m = {"_id": uuid.uuid4().hex[:17], "rid": rid, "msg": text, "ts": iso(ts),
         "u": {"_id": u["_id"], "username": username, "name": u["name"]},
         "_updatedAt": iso(ts)}
    m.update(extra)
    return m


def seed():
    for rid, r in ROOMS.items():
        SUBS[rid] = {"_id": "sub-" + rid, "rid": rid, "name": r["name"], "t": r["t"],
                     "unread": 0, "alert": False, "open": True, "ls": iso(T0),
                     "_updatedAt": iso(T0)}
        if r["fname"]:
            SUBS[rid]["fname"] = r["fname"]
        if rid == "GENERAL":
            SUBS[rid]["f"] = True
        if rid == "atroom":
            SUBS[rid]["open"] = False      # hidden by the user
        MESSAGES[rid] = []
        DELETED[rid] = []
    g = MESSAGES["GENERAL"]
    g.append(make_msg("GENERAL", "alice", "", T0 + 1000, t="uj"))
    for i in range(30):
        g.append(make_msg("GENERAL", "alice" if i % 2 else "tommy", "general message %d" % i, T0 + 2000 + i * 1000))
    g.append(make_msg("GENERAL", "alice", "Hej på dig ’quoted’ “double” – café \U0001F600 €5", T0 + 40000))
    g.append(make_msg("GENERAL", "alice", "line one\nline two", T0 + 41000,
                      reactions={":heart_eyes:": {"usernames": ["tommy"]}, ":+1:": {"usernames": ["tommy", "alice"]}}))
    g.append(make_msg("GENERAL", "alice", "", T0 + 42000,
                      attachments=[{"title": "screenshot.png", "image_url": "/file-upload/x/screenshot.png"}],
                      file={"_id": "f1", "name": "screenshot.png", "type": "image/png"}))
    g.append(make_msg("GENERAL", "tommy", "edited text", T0 + 43000, editedAt=iso(T0 + 44000)))
    g.append(make_msg("GENERAL", "alice", "reply in thread", T0 + 45000, tmid=g[1]["_id"]))
    g.append(make_msg("GENERAL", "alice", "*bold* and _it_ :smile: see [docs](https://x.org/a_b) ```code_here```", T0 + 46000))
    MESSAGES["devroom"].append(make_msg("devroom", "tommy", "AmiSSL works on 68020", T0 + 5000))
    MESSAGES["uid-aliceuid-tommy"].append(make_msg("uid-aliceuid-tommy", "alice", "hi tommy", T0 + 6000))
    SUBS["uid-aliceuid-tommy"]["unread"] = 1
    SUBS["uid-aliceuid-tommy"]["alert"] = True


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "MockRocketChat/1.0"

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("mock: " + fmt % args + "\n")

    def setup(self):
        super().setup()
        with LOCK:
            STATS["connections"] += 1
        self.served = 0

    # -- helpers ---------------------------------------------------------
    def send_json(self, status, obj, chunked=False):
        body = json.dumps(obj, ensure_ascii=self.server.ascii_json).encode("utf-8")
        self.served += 1
        drop = self.server.close_every and self.served % self.server.close_every == 0
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if chunked:
            for i in range(0, len(body), 700):
                part = body[i:i + 700]
                self.wfile.write(b"%x;ext=1\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\nX-Trailer: yes\r\n\r\n")
        else:
            self.wfile.write(body)
        self.wfile.flush()
        if drop:
            # Close silently, as an idle-timeout on a proxy would. The client
            # only finds out when it reuses the connection.
            with LOCK:
                STATS["dropped"] += 1
            self.close_connection = True

    def fail(self, status, error, message=None, **extra):
        obj = {"success": False, "error": error}
        if message:
            obj["message"] = message
        obj.update(extra)
        self.send_json(status, obj)

    def auth_user(self):
        tok = self.headers.get("X-Auth-Token")
        uid = self.headers.get("X-User-Id")
        user = TOKENS.get(tok)
        if not user or USERS[user]["_id"] != uid:
            self.send_json(401, {"status": "error", "message": "You must be logged in to do this."})
            return None
        return user

    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw.decode("utf-8")) if raw else {}
        except ValueError:
            return None

    def qs(self):
        u = urlparse(self.path)
        return u.path, {k: v[0] for k, v in parse_qs(u.query).items()}

    # -- routing ---------------------------------------------------------
    def do_GET(self):
        with LOCK:
            STATS["requests"] += 1
        path, q = self.qs()
        p = path[len(self.server.prefix):] if path.startswith(self.server.prefix) else None
        if p is None:
            return self.send_html()
        if p == "/api/info":
            return self.send_json(200, {"version": "7.10.0", "success": True})
        if p == "/mock/stats":
            return self.send_json(200, dict(STATS, success=True))
        user = self.auth_user()
        if not user:
            return
        if p == "/api/v1/me":
            u = USERS[user]
            return self.send_json(200, {"_id": u["_id"], "username": u["username"], "name": u["name"],
                                        "status": "online", "success": True})
        if p == "/api/v1/subscriptions.get":
            with LOCK:
                subs = list(SUBS.values())
            if "updatedSince" in q:
                try:
                    since = parse_iso(q["updatedSince"])
                except ValueError:
                    return self.fail(400, "error-roomId-param-invalid", "updatedSince is invalid")
                subs = [s for s in subs if parse_iso(s["_updatedAt"]) > since]
                with LOCK:
                    rem = [{"_id": "sub-" + r, "rid": r, "_deletedAt": iso(t)} for r, t in REMOVED_SUBS if t > since]
            else:
                rem = []
            return self.send_json(200, {"update": subs, "remove": rem, "success": True})
        for ep, t in (("channels.history", "c"), ("groups.history", "p"), ("im.history", "d")):
            if p == "/api/v1/" + ep:
                rid = q.get("roomId")
                if rid not in ROOMS:
                    return self.fail(400, "error-room-not-found", "The required \"roomId\" param provided does not match any room")
                if ROOMS[rid]["t"] != t:
                    return self.fail(400, "error-room-not-found", "The required \"roomId\" param provided does not match any " + ep.split(".")[0][:-1])
                count = int(q.get("count", 20))
                with LOCK:
                    msgs = sorted(MESSAGES[rid], key=lambda m: m["ts"], reverse=True)
                if "latest" in q:
                    lt = parse_iso(q["latest"])
                    msgs = [m for m in msgs if parse_iso(m["ts"]) < lt]
                return self.send_json(200, {"messages": msgs[:count], "success": True}, chunked=self.server.chunked)
        for ep, t in (("channels.members", "c"), ("groups.members", "p"), ("im.members", "d")):
            if p == "/api/v1/" + ep:
                rid = q.get("roomId")
                if rid not in ROOMS or ROOMS[rid]["t"] != t:
                    return self.fail(400, "error-room-not-found", "Room not found")
                mem = [{"_id": u["_id"], "username": u["username"], "name": u["name"],
                        "status": "online" if u["username"] == "alice" else "offline"}
                       for u in USERS.values()]
                return self.send_json(200, {"members": mem, "count": len(mem), "offset": 0,
                                            "total": len(mem), "success": True})
        if p == "/api/v1/rooms.info":
            rid = q.get("roomId")
            if rid not in ROOMS:
                return self.fail(400, "error-room-not-found", "The required \"roomId\" or \"roomName\" param provided does not match any channel")
            room = {"_id": rid, "t": ROOMS[rid]["t"], "name": ROOMS[rid]["name"]}
            if rid == "GENERAL":
                room["topic"] = "Gammal skol-PC topic \u2013 v\u00e4lkommen"
            return self.send_json(200, {"room": room, "success": True})
        if p == "/api/v1/chat.syncMessages":
            rid = q.get("roomId")
            if rid not in ROOMS:
                return self.fail(400, "error-roomId-param-invalid", "The \"roomId\" query parameter must be a valid room")
            try:
                since = parse_iso(q["lastUpdate"])
            except (KeyError, ValueError):
                return self.fail(400, "error-lastUpdate-param-invalid", "The \"lastUpdate\" query parameter must be a valid date")
            with LOCK:
                upd = [m for m in MESSAGES[rid] if parse_iso(m["_updatedAt"]) > since]
                dele = [{"_id": i, "_deletedAt": iso(t)} for i, t in DELETED[rid] if t > since]
            return self.send_json(200, {"result": {"updated": upd, "deleted": dele}, "success": True})
        self.fail(404, "Not found", "Endpoint not found")

    def do_POST(self):
        with LOCK:
            STATS["requests"] += 1
        path, q = self.qs()
        p = path[len(self.server.prefix):] if path.startswith(self.server.prefix) else None
        if p is None:
            self.body_json()
            return self.send_html(404)
        body = self.body_json()
        if body is None:
            return self.fail(400, "invalid-json", "Invalid JSON body")
        if p == "/api/v1/login":
            return self.login(body)
        if p.startswith("/mock/"):
            return self.mock(p, body)
        user = self.auth_user()
        if not user:
            return
        if p == "/api/v1/logout":
            for t, u in list(TOKENS.items()):
                if u == user and t == self.headers.get("X-Auth-Token"):
                    del TOKENS[t]
            return self.send_json(200, {"status": "success", "data": {"message": "You've been logged out!"}})
        if p == "/api/v1/chat.sendMessage":
            m = body.get("message") or {}
            rid, text = m.get("rid"), m.get("msg")
            if rid not in ROOMS or not isinstance(text, str):
                return self.fail(400, "error-invalid-params", "Invalid message")
            msg = make_msg(rid, user, text)
            with LOCK:
                MESSAGES[rid].append(msg)
            return self.send_json(200, {"message": msg, "success": True})
        if p == "/api/v1/subscriptions.read":
            rid = body.get("rid") or body.get("roomId")
            with LOCK:
                s = SUBS.get(rid)
                if s:
                    s.update(unread=0, alert=False, _updatedAt=iso(now_ms()))
                self.server.read_calls.append(rid)
            return self.send_json(200, {"success": True})
        self.fail(404, "Not found", "Endpoint not found")

    def login(self, body):
        if "resume" in body:
            user = TOKENS.get(body["resume"])
            if not user:
                return self.send_json(401, {"status": "error", "error": 403, "message": "You've been logged out by the server. Please log in again. [403]"})
        else:
            name, pw = body.get("user"), body.get("password")
            u = USERS.get(name)
            if not u or u["password"] != pw:
                return self.send_json(401, {"status": "error", "error": "Unauthorized", "message": "Unauthorized"})
            if u["totp"]:
                code = body.get("code")
                if not code:
                    return self.send_json(401, {"status": "error", "error": "totp-required",
                                                "message": "TOTP Required [totp-required]",
                                                "details": {"method": "totp", "codeGenerated": False}})
                if code != u["totp"]:
                    return self.send_json(401, {"status": "error", "error": "totp-invalid",
                                                "message": "TOTP Invalid [totp-invalid]",
                                                "details": {"method": "totp"}})
            user = name
        tok = uuid.uuid4().hex
        TOKENS[tok] = user
        u = USERS[user]
        self.send_json(200, {"status": "success", "data": {
            "authToken": tok, "userId": u["_id"],
            "me": {"_id": u["_id"], "username": u["username"], "name": u["name"]}}})

    def mock(self, p, body):
        rid = body.get("rid")
        with LOCK:
            if p == "/mock/inject":
                msg = make_msg(rid, body.get("user", "alice"), body["msg"])
                MESSAGES[rid].append(msg)
                s = SUBS[rid]
                s.update(unread=s["unread"] + 1, alert=True, _updatedAt=iso(now_ms()))
                return self.send_json(200, {"message": msg, "success": True})
            if p == "/mock/edit":
                for m in MESSAGES[rid]:
                    if m["_id"] == body["id"]:
                        m.update(msg=body["msg"], editedAt=iso(now_ms()), _updatedAt=iso(now_ms()))
                        return self.send_json(200, {"message": m, "success": True})
                return self.fail(404, "not-found")
            if p == "/mock/delete":
                MESSAGES[rid] = [m for m in MESSAGES[rid] if m["_id"] != body["id"]]
                DELETED[rid].append((body["id"], now_ms()))
                return self.send_json(200, {"success": True})
            if p == "/mock/leave":
                SUBS.pop(rid, None)
                REMOVED_SUBS.append((rid, now_ms()))
                return self.send_json(200, {"success": True})
            if p == "/mock/read-calls":
                return self.send_json(200, {"calls": self.server.read_calls, "success": True})
        self.fail(404, "Not found")

    def send_html(self, status=200):
        body = b"<html><body>Not Rocket.Chat</body></html>"
        self.send_response(status)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--host", default="127.0.0.1", help="address to listen on (0.0.0.0 for the emulator)")
    ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
    ap.add_argument("--prefix", default="", help="serve under a path prefix, e.g. /chat")
    ap.add_argument("--close-every", type=int, default=0)
    ap.add_argument("--chunked", action="store_true", help="send history responses chunked")
    ap.add_argument("--ascii-json", action="store_true", help="escape non-ASCII as \\uXXXX")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    seed()
    srv = ThreadingHTTPServer((a.host, a.port), Handler)
    srv.daemon_threads = True
    srv.prefix = a.prefix.rstrip("/")
    srv.close_every = a.close_every
    srv.chunked = a.chunked
    srv.ascii_json = a.ascii_json
    srv.verbose = a.verbose
    srv.read_calls = []
    if a.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(a.tls[0], a.tls[1])
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print("PORT %d" % srv.server_address[1], flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
