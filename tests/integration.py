#!/usr/bin/env python3
"""
Integration tests: runs rcli (built from the AmiRocket core) against the
mock Rocket.Chat server in several configurations.

  python3 tests/integration.py build/host/rcli
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RCLI = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/host/rcli")
FAILS = []
PASSES = 0


def check(cond, what, detail=""):
    global PASSES
    if cond:
        PASSES += 1
    else:
        FAILS.append(what)
        print("FAIL:", what, ("\n  " + detail.replace("\n", "\n  ")) if detail else "")


class Mock:
    def __init__(self, *args, tls=None):
        cmd = [sys.executable, os.path.join(HERE, "mock_rocketchat.py"), "--port", "0"] + list(args)
        if tls:
            cmd += ["--tls"] + list(tls)
        self.p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)
        line = self.p.stdout.readline()
        self.port = int(line.split()[1])
        self.scheme = "https" if tls else "http"
        self.url = "%s://localhost:%d" % (self.scheme, self.port)
        self.tls = tls

    def post(self, path, obj):
        req = urllib.request.Request(self.url + path, data=json.dumps(obj).encode(),
                                     headers={"Content-Type": "application/json"})
        ctx = None
        if self.tls:
            import ssl
            ctx = ssl.create_default_context(cafile=self.tls[0])
        return json.loads(urllib.request.urlopen(req, context=ctx).read())

    def get(self, path):
        ctx = None
        if self.tls:
            import ssl
            ctx = ssl.create_default_context(cafile=self.tls[0])
        return json.loads(urllib.request.urlopen(self.url + path, context=ctx).read())

    def stop(self):
        self.p.terminate()
        self.p.wait()


def entries(out):
    """Messages in rcli output (continuation lines of multi-line texts folded)."""
    res = []
    for line in out.splitlines():
        if len(line) > 24 and line[:2] == "20" and line[10] == "T":
            res.append(line)
        elif res:
            res[-1] += "\n" + line
    return res


def rcli(*args, env=None, timeout=60):
    e = dict(os.environ)
    for k in ("RC_URL", "RC_USER", "RC_PASSWORD", "RC_USER_ID", "RC_TOKEN", "AMIROCKET_CA_FILE"):
        e.pop(k, None)
    if env:
        e.update(env)
    r = subprocess.run([RCLI] + list(args), capture_output=True, env=e, timeout=timeout)
    return r.returncode, r.stdout.decode("latin-1"), r.stderr.decode("latin-1")


def login_args(m, user="tommy", pw="secret"):
    return ["-s", m.url, "-u", user, "-p", pw]


def test_basic():
    m = Mock()
    try:
        rc, out, err = rcli("-s", m.url, "info")
        check(rc == 0 and "version=7.10.0" in out, "info", out + err)

        rc, out, err = rcli(*login_args(m, pw="wrong"), "rooms")
        check(rc == 2 and "Unauthorized" in err, "wrong password -> auth error", err)

        rc, out, err = rcli(*login_args(m), "rooms")
        lines = out.strip().splitlines()
        check(rc == 0 and len(lines) == 5, "rooms listed", out + err)
        check([l.split()[1] for l in lines] == ["amiga-dev", "empty", "general", "@zeta-group", "alice"],
              "rooms sorted: channels by title ignoring punctuation, then DMs", out)
        check("unread=1 alert" in lines[4], "DM unread/alert parsed", out)
        check(" fav " in lines[2] and " fav " not in lines[0], "favourite flag parsed", out)
        check(" hidden " in lines[3] and " hidden " not in lines[2], "hidden (open:false) flag parsed", out)

        rc, out, err = rcli(*login_args(m), "history", "general", "20")
        lines = entries(out)
        check(rc == 0 and len(lines) == 20, "history count 20", out + err)
        ts = [l.split()[0] for l in lines]
        check(ts == sorted(ts), "history oldest first", out)
        check("Hej p\u00c3\u00a5 dig \u00e2\u0080\u0099quoted" in out, "UTF-8 text intact", out)
        check("line one\nline two" in out, "multiline text", out)
        check("[attachment: screenshot.png]" in out, "attachment title", out)
        check("edited text (edited)" in out, "edited flag", out)
        check("reply in thread (thread)" in out, "thread flag", out)

        rc, out, err = rcli("-L", *login_args(m), "history", "#general", "20")
        check("Hej p\u00e5 dig 'quoted' \"double\" - caf\u00e9 :D EUR5" in out, "Latin-1 conversion", out)
        check("bold and it :) see docs (https://x.org/a_b) code_here" in out, "markup removed as in the GUI", out)

        rc, out, err = rcli(*login_args(m), "history", "general", "50")
        check(rc == 0 and "* joined the room" in out, "system message text", out)

        rc, out, err = rcli(*login_args(m), "history", "Amiga Dev")
        check(rc == 0 and "AmiSSL works on 68020" in out, "private group history via fname", out + err)
        rc, out, err = rcli(*login_args(m), "history", "@alice")
        check(rc == 0 and "hi tommy" in out, "DM history (im.history)", out + err)
        reads = m.post("/mock/read-calls", {})["calls"]
        check("uid-aliceuid-tommy" in reads, "opening unread DM marks it read", str(reads))
        rc, out, err = rcli(*login_args(m), "history", "empty")
        check(rc == 0 and out == "", "empty room", out + err)
        rc, out, err = rcli(*login_args(m), "history", "nosuchroom")
        check(rc == 1 and "no such room" in err, "unknown room", err)

        rc, out, err = rcli(*login_args(m), "send", "general", "Hall\u00e5 fr\u00e5n Amiga \u2013 \"quotes\" \\ back")
        check(rc == 0 and "<tommy> Hall" in out, "send", out + err)
        rc, out, err = rcli(*login_args(m), "history", "general", "1")
        check("Hall\u00c3\u00a5 fr\u00c3\u00a5n Amiga \u00e2\u0080\u0093 \"quotes\" \\ back" in out,
              "sent text round-trips (JSON escaping, UTF-8)", out)

        st = m.get("/mock/stats")
        check(st["connections"] < st["requests"], "keep-alive reuses connections", str(st))

        # token login
        rc, out, err = rcli(*login_args(m), "login")
        kv = dict(l.split("=", 1) for l in out.strip().splitlines())
        check(rc == 0 and kv.get("user_id") == "uid-tommy" and len(kv.get("token", "")) == 32, "login prints token", out)
        rc, out, err = rcli("-s", m.url, "-i", kv["user_id"], "-t", kv["token"], "rooms")
        check(rc == 0 and len(out.strip().splitlines()) == 5, "token login", out + err)
        rc, out, err = rcli("-s", m.url, "-i", kv["user_id"], "-t", "bad", "rooms")
        check(rc == 2 and "no longer valid" in err, "bad token -> auth error", err)

        test_watch(m)
    finally:
        m.stop()


def test_watch(m):
    """Polling: new, edited and deleted messages; room updates."""
    p = subprocess.Popen([RCLI, *login_args(m), "watch", "general", "1", "5"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.6)
    new = m.post("/mock/inject", {"rid": "GENERAL", "user": "alice", "msg": "polled message"})["message"]
    m.post("/mock/inject", {"rid": "devroom", "user": "alice", "msg": "elsewhere"})
    time.sleep(1.2)
    m.post("/mock/edit", {"rid": "GENERAL", "id": new["_id"], "msg": "polled message v2"})
    time.sleep(1.2)
    m.post("/mock/delete", {"rid": "GENERAL", "id": new["_id"]})
    m.post("/mock/leave", {"rid": "emptyroom"})
    out, err = p.communicate(timeout=30)
    out = out.decode("latin-1")
    err = err.decode("latin-1")
    check(p.returncode == 0, "watch exits cleanly", err)
    check("<alice> polled message\n" in out, "watch: new message", out)
    check("<alice> polled message v2 (edited)" in out, "watch: edit", out)
    check("deleted " + new["_id"] in out, "watch: delete", out)
    check("room-update p amiga-dev" in out and "unread=1" in out, "watch: room list update", out)
    check("room-removed emptyroom" in out, "watch: room removed", out)
    check(out.count("<alice> polled message\n") == 1, "watch: no duplicates from inclusive cursor", out)


def test_totp():
    m = Mock()
    try:
        rc, out, err = rcli(*login_args(m, "secure"), "rooms")
        check(rc == 3 and "Two-factor code required (totp)" in err, "2FA required", err)
        rc, out, err = rcli(*login_args(m, "secure"), "-c", "000000", "rooms")
        check(rc == 1 and "not accepted" in err, "2FA wrong code", err)
        rc, out, err = rcli(*login_args(m, "secure"), "-c", "123456", "rooms")
        check(rc == 0 and len(out.strip().splitlines()) == 5, "2FA correct code", out + err)
    finally:
        m.stop()


def test_flaky_and_chunked():
    m = Mock("--close-every", "2", "--chunked", "--ascii-json")
    try:
        rc, out, err = rcli(*login_args(m), "history", "general", "40")
        check(rc == 0 and len(entries(out)) == 37, "chunked + escaped JSON + dropped connections", out + err)
        check("Hej p\u00c3\u00a5 dig" in out, "\\u escapes decoded", out)
        rc, out, err = rcli(*login_args(m), "send", "general", "after reconnect")
        check(rc == 0, "send over flaky server", err)
        st = m.get("/mock/stats")
        check(st["dropped"] > 0 and st["connections"] > 1, "server dropped idle connections", str(st))
    finally:
        m.stop()


def test_prefix_and_errors():
    m = Mock("--prefix", "/chat")
    try:
        rc, out, err = rcli("-s", m.url + "/chat/", "-u", "tommy", "-p", "secret", "rooms")
        check(rc == 0 and len(out.strip().splitlines()) == 5, "server under a path prefix", out + err)
        rc, out, err = rcli("-s", m.url, "-u", "tommy", "-p", "secret", "rooms")
        check(rc != 0 and "not JSON" in err, "non-Rocket.Chat page detected", err)
    finally:
        m.stop()
    rc, out, err = rcli("-s", "http://127.0.0.1:1", "-u", "a", "-p", "b", "rooms")
    check(rc == 1 and "Network error" in err and "connect" in err.lower(), "connection refused", err)
    rc, out, err = rcli("-s", "http://no-such-host.invalid", "-u", "a", "-p", "b", "rooms")
    check(rc == 1 and "resolve" in err, "DNS failure", err)


def test_tls():
    d = tempfile.mkdtemp()
    cert, key = os.path.join(d, "cert.pem"), os.path.join(d, "key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                    "-keyout", key, "-out", cert, "-subj", "/CN=localhost",
                    "-addext", "subjectAltName=DNS:localhost"],
                   check=True, capture_output=True)
    m = Mock(tls=(cert, key))
    try:
        rc, out, err = rcli(*login_args(m), "rooms")
        check(rc == 1 and "Certificate" in err, "self-signed cert rejected by default", err)
        rc, out, err = rcli("-k", *login_args(m), "rooms")
        check(rc == 0 and len(out.strip().splitlines()) == 5, "TLS with -k", out + err)
        rc, out, err = rcli(*login_args(m), "rooms", env={"AMIROCKET_CA_FILE": cert})
        check(rc == 0, "TLS verified against CA file", out + err)
        bad = "https://127.0.0.1:%d" % m.port
        rc, out, err = rcli("-s", bad, "-u", "tommy", "-p", "secret", "rooms", env={"AMIROCKET_CA_FILE": cert})
        check(rc == 1, "TLS host name mismatch rejected", err)
    finally:
        m.stop()


def main():
    if not os.path.exists(RCLI):
        print("rcli not found:", RCLI)
        return 2
    test_basic()
    test_totp()
    test_flaky_and_chunked()
    test_prefix_and_errors()
    test_tls()
    print("integration tests: %d passed, %d failed" % (PASSES, len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
