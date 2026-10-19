"""Private-tailnet account portal for the pinned shadNet protocol.

The shadNet registration secret never leaves this service. QQ identifiers are
HMACed for rate limiting and deduplication; only their last four digits are kept.
"""
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import socket
import sqlite3
import struct
import threading
import time
from datetime import datetime, timedelta, timezone
from urllib.request import Request, urlopen


DATA = Path(os.environ.get("PORTAL_DATA", "/var/lib/portal"))
SHADNET_SECRET = Path(os.environ.get("SHADNET_SECRET_FILE", "/run/shadnet/registration_secret"))
SHADNET_HOST = os.environ.get("SHADNET_HOST", "127.0.0.1")
SHADNET_PORT = int(os.environ.get("SHADNET_PORT", "31313"))
STATS_BASE = os.environ.get("STATS_BASE", "http://127.0.0.1:31320/stats")
AUTO_APPROVE = os.environ.get("PORTAL_AUTO_APPROVE", "false").lower() == "true"
POLL_SECONDS = max(5, int(os.environ.get("PORTAL_POLL_SECONDS", "20")))
HEADSCALE_API_URL = os.environ.get("HEADSCALE_API_URL", "http://headscale:8080").rstrip("/")
HEADSCALE_API_KEY_FILE = Path(os.environ.get("HEADSCALE_API_KEY_FILE", "/run/secrets/headscale_api_key"))
HEADSCALE_USER_ID = os.environ.get("HEADSCALE_USER_ID", "")
DEVICE_KEYS_PER_DAY = 5
SEAMLESS_PRESENCE_SECONDS = 90
NPID = re.compile(r"[A-Za-z0-9_-]{3,16}\Z")
QQ = re.compile(r"[1-9][0-9]{4,11}\Z")


def read_or_create_secret(path):
    if path.exists():
        return path.read_text(encoding="ascii").strip()
    value = secrets.token_hex(32)
    path.write_text(value, encoding="ascii")
    os.chmod(path, 0o600)
    return value


def mac(secret, value):
    return hmac.new(secret.encode("ascii"), value.encode("utf-8"), hashlib.sha256).hexdigest()


def proto_field(field, value):
    data = value.encode("utf-8")
    result = bytearray()
    for number in ((field << 3) | 2, len(data)):
        while number >= 0x80:
            result.append((number & 0x7f) | 0x80)
            number >>= 7
        result.append(number)
    result.extend(data)
    return bytes(result)


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.recv(size - len(data))
        if not chunk:
            raise ConnectionError("shadNet closed the registration connection")
        data.extend(chunk)
    return bytes(data)


def register_shadnet(npid, password, registration_secret):
    payload = (proto_field(1, npid) + proto_field(2, password) +
               proto_field(4, npid.lower() + "@bbport.invalid") +
               proto_field(5, registration_secret))
    packet = struct.pack("<BHIQ", 0, 2, 19 + len(payload), 1) + struct.pack("<I", len(payload)) + payload
    with socket.create_connection((SHADNET_HOST, SHADNET_PORT), timeout=5) as stream:
        stream.settimeout(5)
        header = read_exact(stream, 15)
        size = struct.unpack_from("<I", header, 3)[0]
        if header[0] != 3 or size < 19 or size > 65536:
            raise ValueError("incompatible shadNet handshake")
        read_exact(stream, size - 15)
        stream.sendall(packet)
        reply = read_exact(stream, 15)
        reply_size = struct.unpack_from("<I", reply, 3)[0]
        if reply[0] != 1 or reply_size < 16 or reply_size > 65536:
            raise ValueError("incompatible shadNet reply")
        return read_exact(stream, reply_size - 15)[0]


def stats(path):
    with urlopen(STATS_BASE + path, timeout=3) as response:
        if response.status != 200:
            raise RuntimeError("stats unavailable")
        return json.load(response)


def create_headscale_preauth():
    """Mint a short-lived, one-use device key; keep the admin key on the server."""
    if not HEADSCALE_USER_ID.isdecimal() or not HEADSCALE_API_KEY_FILE.is_file():
        raise RuntimeError("私网自动入网尚未配置，请联系管理员")
    admin_key = HEADSCALE_API_KEY_FILE.read_text(encoding="ascii").strip()
    if not admin_key:
        raise RuntimeError("私网自动入网尚未配置，请联系管理员")
    expires = datetime.now(timezone.utc) + timedelta(minutes=10)
    payload = json.dumps({"user": HEADSCALE_USER_ID, "reusable": False,
                          "ephemeral": False, "expiration": expires.isoformat().replace("+00:00", "Z")}).encode()
    request = Request(HEADSCALE_API_URL + "/api/v1/preauthkey", data=payload,
                      headers={"Authorization": "Bearer " + admin_key,
                               "Content-Type": "application/json"}, method="POST")
    try:
        with urlopen(request, timeout=5) as response:
            result = json.load(response)
    except Exception as exc:
        raise RuntimeError("私网入网凭据签发失败，请联系管理员") from exc
    key = result.get("preAuthKey", {}).get("key", "")
    if not isinstance(key, str) or not key.startswith("hskey-auth-"):
        raise RuntimeError("私网入网凭据响应无效")
    return key


class Portal:
    def __init__(self, data=DATA):
        data.mkdir(parents=True, exist_ok=True)
        self.secret = read_or_create_secret(data / "portal_secret")
        self.admin_key = read_or_create_secret(data / "admin_key")
        self.db = sqlite3.connect(data / "portal.db", check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS applications (
                id TEXT PRIMARY KEY, token_hash TEXT NOT NULL,
                qq_hash TEXT NOT NULL, qq_last4 TEXT NOT NULL,
                nickname TEXT NOT NULL, npid TEXT NOT NULL COLLATE NOCASE UNIQUE,
                show_in_list INTEGER NOT NULL, state TEXT NOT NULL,
                created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,
                source_hash TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS activity (
                npid TEXT PRIMARY KEY COLLATE NOCASE, first_seen INTEGER NOT NULL,
                last_seen INTEGER NOT NULL, login_count INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS login_events (
                npid TEXT NOT NULL, at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS login_events_at ON login_events(at);
            CREATE TABLE IF NOT EXISTS device_key_issues (
                application_id TEXT NOT NULL, issued_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS device_key_issues_recent ON device_key_issues(application_id,issued_at);
        """)
        self.db.commit()
        self.lock = threading.RLock()
        self.active = set()
        self.seamless_presence = {}

    def one_time_key(self, application_id, token):
        return mac(self.secret, "register:" + application_id + ":" + token)

    def apply(self, body, peer):
        qq = str(body.get("qq", "")).strip()
        nickname = str(body.get("nickname", "")).strip()
        npid = str(body.get("npid", "")).strip()
        if not QQ.fullmatch(qq) or not NPID.fullmatch(npid) or not (1 <= len(nickname) <= 24):
            raise ValueError("QQ、昵称或联机 ID 格式不正确")
        if any(ord(c) < 32 for c in nickname):
            raise ValueError("昵称包含控制字符")
        now = int(time.time())
        qq_hash = mac(self.secret, "qq:" + qq)
        source_hash = mac(self.secret, "ip:" + peer)
        with self.lock:
            if self.db.execute("SELECT 1 FROM applications WHERE npid=?", (npid,)).fetchone():
                raise ValueError("联机 ID 已被占用")
            if self.db.execute("SELECT 1 FROM applications WHERE qq_hash=? AND created_at>?",
                               (qq_hash, now - 300)).fetchone():
                raise RuntimeError("申请太频繁，请五分钟后重试")
            if self.db.execute("SELECT 1 FROM applications WHERE source_hash=? AND created_at>?",
                               (source_hash, now - 5)).fetchone():
                raise RuntimeError("请求太频繁，请稍后重试")
            application_id = secrets.token_hex(12)
            token = secrets.token_urlsafe(32)
            state = "approved" if AUTO_APPROVE else "pending"
            try:
                self.db.execute("INSERT INTO applications VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                                (application_id, mac(self.secret, token), qq_hash, qq[-4:], nickname,
                                 npid, int(bool(body.get("show_in_list", False))), state,
                                 now, now + 3600, source_hash))
            except sqlite3.IntegrityError as exc:
                raise ValueError("联机 ID 已被占用") from exc
            self.db.commit()
        return {"application_id": application_id, "application_token": token,
                "state": state, "registration_key": self.one_time_key(application_id, token)
                if state == "approved" else None}

    def application(self, application_id, token):
        with self.lock:
            row = self.db.execute("SELECT * FROM applications WHERE id=?", (application_id,)).fetchone()
        if not row or not hmac.compare_digest(row["token_hash"], mac(self.secret, token)):
            raise PermissionError("申请凭据无效")
        state = row["state"] if row["state"] == "registered" or row["expires_at"] >= time.time() else "expired"
        return {"state": state, "npid": row["npid"],
                "registration_key": self.one_time_key(application_id, token)
                if state == "approved" else None}

    def register(self, body):
        application_id = str(body.get("application_id", ""))
        token = str(body.get("application_token", ""))
        key = str(body.get("registration_key", ""))
        password = str(body.get("password", ""))
        if not (8 <= len(password) <= 128):
            raise ValueError("密码须为 8–128 个字符")
        with self.lock:
            row = self.db.execute("SELECT * FROM applications WHERE id=?", (application_id,)).fetchone()
            if not row or row["state"] != "approved" or row["expires_at"] < time.time():
                raise PermissionError("申请尚未通过、已过期或已使用")
            if not hmac.compare_digest(row["token_hash"], mac(self.secret, token)) or not hmac.compare_digest(
                    self.one_time_key(application_id, token), key):
                raise PermissionError("一次性注册码无效")
            code = register_shadnet(row["npid"], password, SHADNET_SECRET.read_text(encoding="ascii").strip())
            if code != 0:
                return {"ok": False, "server_code": code}
            self.db.execute("UPDATE applications SET state='registered' WHERE id=?", (application_id,))
            self.db.commit()
            return {"ok": True, "npid": row["npid"]}

    def device_key(self, body):
        application_id = str(body.get("application_id", ""))
        token = str(body.get("application_token", ""))
        now = int(time.time())
        with self.lock:
            row = self.db.execute("SELECT token_hash,state FROM applications WHERE id=?",
                                  (application_id,)).fetchone()
            if not row or not hmac.compare_digest(row["token_hash"], mac(self.secret, token)) or \
                    row["state"] != "registered":
                raise PermissionError("请先在启动器完成在线账号注册")
            count = self.db.execute("SELECT COUNT(*) FROM device_key_issues WHERE application_id=? "
                                    "AND issued_at>?", (application_id, now - 86400)).fetchone()[0]
            if count >= DEVICE_KEYS_PER_DAY:
                raise RuntimeError("今日设备入网次数已达上限，请 24 小时后再试")
            key = create_headscale_preauth()
            self.db.execute("INSERT INTO device_key_issues VALUES (?,?)", (application_id, now))
            self.db.execute("DELETE FROM device_key_issues WHERE issued_at<?", (now - 30 * 86400,))
            self.db.commit()
        return {"auth_key": key, "expires_in_seconds": 600}

    def presence(self, body):
        application_id = str(body.get("application_id", ""))
        token = str(body.get("application_token", ""))
        state = str(body.get("state", ""))
        if state not in ("online", "offline"):
            raise ValueError("玩家状态无效")
        with self.lock:
            row = self.db.execute("SELECT npid,token_hash,state FROM applications WHERE id=?",
                                  (application_id,)).fetchone()
            if not row or row["state"] != "registered" or not hmac.compare_digest(
                    row["token_hash"], mac(self.secret, token)):
                raise PermissionError("请先完成在线账号注册")
            npid = row["npid"].lower()
            if state == "online":
                self.seamless_presence[npid] = time.time() + SEAMLESS_PRESENCE_SECONDS
            else:
                self.seamless_presence.pop(npid, None)
        return {"ok": True}

    def approve(self, application_id):
        with self.lock:
            row = self.db.execute("SELECT state,expires_at FROM applications WHERE id=?",
                                  (application_id,)).fetchone()
            if not row or row["state"] != "pending" or row["expires_at"] < time.time():
                raise ValueError("申请不存在或不可批准")
            self.db.execute("UPDATE applications SET state='approved' WHERE id=?", (application_id,))
            self.db.commit()
        return {"ok": True}

    def snapshot(self):
        usage = stats("/usage")
        online = stats("/online")
        now = int(time.time())
        traditional_users = online.get("users", [])
        traditional_active = {str(u.get("npid", "")).lower() for u in traditional_users if u.get("npid")}
        with self.lock:
            self.seamless_presence = {npid: deadline for npid, deadline in self.seamless_presence.items()
                                      if deadline > time.time()}
            seamless_active = set(self.seamless_presence)
            active = traditional_active | seamless_active
            for npid in active:
                if npid not in self.active:
                    self.db.execute("INSERT INTO activity VALUES (?,?,?,1) ON CONFLICT(npid) "
                                    "DO UPDATE SET last_seen=excluded.last_seen, login_count=login_count+1",
                                    (npid, now, now))
                    self.db.execute("INSERT INTO login_events VALUES (?,?)", (npid, now))
                else:
                    self.db.execute("UPDATE activity SET last_seen=? WHERE npid=?", (now, npid))
            self.db.execute("DELETE FROM login_events WHERE at<?", (now - 90 * 86400,))
            self.db.commit()
            self.active = active
            registered = {r["npid"].lower(): r for r in self.db.execute(
                "SELECT npid,nickname,show_in_list FROM applications WHERE state='registered'")}
        users = []
        for user in traditional_users:
            npid = str(user.get("npid", ""))
            record = registered.get(npid.lower())
            if not record or not record["show_in_list"] or user.get("appear_offline", False):
                continue
            both_modes = npid.lower() in seamless_active
            users.append({"npid": npid, "nickname": record["nickname"],
                          "title_id": user.get("title_id", ""),
                          "title_name": "Bloodborne（传统＋无缝）" if both_modes else user.get("title_name", ""),
                          "mode": "both" if both_modes else "traditional"})
        for npid in seamless_active - traditional_active:
            record = registered.get(npid)
            if record and record["show_in_list"]:
                users.append({"npid": record["npid"], "nickname": record["nickname"],
                              "title_id": "CUSA03173", "title_name": "Bloodborne（无缝）",
                              "mode": "seamless"})
        users.sort(key=lambda u: u["npid"].lower())
        traditional_count = int(usage.get("total_users", 0))
        return {"ok": True, "online_players": traditional_count + len(seamless_active - traditional_active),
                "traditional_online_players": traditional_count,
                "seamless_online_players": len(seamless_active),
                "listed_players": len(users), "users": users}

    def admin_usage(self):
        snapshot = self.snapshot()
        with self.lock:
            counts = self.db.execute("SELECT state,COUNT(*) AS n FROM applications GROUP BY state").fetchall()
            events = self.db.execute("SELECT COUNT(*) FROM login_events WHERE at>?",
                                     (int(time.time()) - 86400,)).fetchone()[0]
            players = [dict(r) for r in self.db.execute(
                "SELECT a.id AS application_id,a.npid,a.nickname,a.qq_last4,a.state,"
                "COALESCE(t.login_count,0) AS login_count,"
                "t.last_seen FROM applications a LEFT JOIN activity t ON a.npid=t.npid ORDER BY a.created_at DESC")]
        return {"online_players": snapshot["online_players"], "login_events_24h": events,
                "applications": {r["state"]: r["n"] for r in counts}, "players": players}


class Handler(BaseHTTPRequestHandler):
    portal = None

    def log_message(self, format_string, *args):
        # Never log paths or request bodies: application tokens and QQ are sensitive.
        print("portal request", self.command, self.client_address[0], flush=True)

    def reply(self, code, payload):
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        supplied = self.headers.get("Authorization", "")
        return hmac.compare_digest(supplied, "Bearer " + self.portal.admin_key)

    def body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > 4096:
            raise ValueError("请求过大")
        value = json.loads(self.rfile.read(length))
        if not isinstance(value, dict):
            raise ValueError("请求格式错误")
        return value

    def do_GET(self):
        try:
            if self.path == "/v1/status":
                self.reply(200, self.portal.snapshot())
            elif self.path == "/v1/admin/usage" and self.authorized():
                self.reply(200, self.portal.admin_usage())
            else:
                self.reply(404, {"error": "not found"})
        except PermissionError as e:
            self.reply(403, {"error": str(e)})
        except Exception:
            self.reply(503, {"error": "service unavailable"})

    def do_POST(self):
        try:
            if self.path == "/v1/apply":
                self.reply(200, self.portal.apply(self.body(), self.client_address[0]))
            elif self.path == "/v1/application":
                body = self.body()
                self.reply(200, self.portal.application(str(body.get("application_id", "")),
                                                        str(body.get("application_token", ""))))
            elif self.path == "/v1/register":
                self.reply(200, self.portal.register(self.body()))
            elif self.path == "/v1/device-key":
                self.reply(200, self.portal.device_key(self.body()))
            elif self.path == "/v1/presence":
                self.reply(200, self.portal.presence(self.body()))
            elif self.path.startswith("/v1/admin/approve/") and self.authorized():
                self.reply(200, self.portal.approve(self.path.rsplit("/", 1)[-1]))
            else:
                self.reply(404, {"error": "not found"})
        except (ValueError, RuntimeError) as e:
            self.reply(400, {"error": str(e)})
        except PermissionError as e:
            self.reply(403, {"error": str(e)})
        except Exception:
            self.reply(503, {"error": "service unavailable"})


def main():
    Handler.portal = Portal()
    def poll_activity():
        while True:
            try:
                Handler.portal.snapshot()
            except Exception as error:
                print("activity poll unavailable:", type(error).__name__, flush=True)
            time.sleep(POLL_SECONDS)
    threading.Thread(target=poll_activity, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", 18080), Handler)
    print("portal listening on :18080; auto-approve=" + str(AUTO_APPROVE), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
