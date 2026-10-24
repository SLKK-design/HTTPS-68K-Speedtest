"""lab_server.py: a TLS 1.2 test server for the Speed Test's "Test Server"
rows. It plays api.notion.com without the 15 s limit: same cipher
(ECDHE-ECDSA with ChaCha20-Poly1305 over X25519), a certificate chain of the
same shape (tools/lab/make_certs.sh), session tickets, and an HTTP 401 of
about the same size for GET /v1/users/me. Every page can finish, however
long its handshake takes.

    python3 tools/lab_server.py [log file]     (port 4433)
"""
import json
import os
import socket
import ssl
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LAB = os.path.join(HERE, "lab")
PORT = 4433
LOG = sys.argv[1] if len(sys.argv) > 1 else None

BODY = json.dumps({
    "object": "error", "status": 401, "code": "unauthorized",
    "message": "API token is invalid.",
    "request_id": "00000000-0000-0000-0000-000000000000",
}).encode()
# Padded with headers to about the size of Notion's answer (1229 bytes)
HEAD = ("HTTP/1.1 401 Unauthorized\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        f"Content-Length: {len(BODY)}\r\n"
        "Connection: close\r\n"
        "Server: speedtest-lab\r\n"
        "X-Padding: " + "x" * 900 + "\r\n\r\n").encode()

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.set_ciphers("ECDHE-ECDSA-CHACHA20-POLY1305")
ctx.set_ecdh_curve("X25519")
ctx.load_cert_chain(os.path.join(LAB, "chain.pem"), os.path.join(LAB, "leaf.key"))
lock = threading.Lock()


def log(line):
    with lock:
        print(line, flush=True)
        if LOG:
            with open(LOG, "a") as f:
                f.write(line + "\n")


def serve(raw, addr, n):
    t0 = time.perf_counter()
    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    raw.settimeout(300)   # far beyond a stock-mbedTLS handshake on a Classic II
    try:
        s = ctx.wrap_socket(raw, server_side=True)
        hs = time.perf_counter() - t0
        resumed = s.session_reused
        req = b""
        while b"\r\n\r\n" not in req:
            d = s.recv(4096)
            if not d:
                break
            req += d
        s.sendall(HEAD + BODY)
        try:
            s = s.unwrap()   # close_notify, then close, as Notion's server does
        except (OSError, ssl.SSLError):
            pass
        log(f"{time.strftime('%H:%M:%S')} conn {n} from {addr[0]}: handshake {hs:.3f} s"
            f"{' (resumed)' if resumed else ''}, answered {len(HEAD) + len(BODY)} bytes")
        s.close()
    except (OSError, ssl.SSLError) as e:
        log(f"{time.strftime('%H:%M:%S')} conn {n} from {addr[0]}: {type(e).__name__} {e} "
            f"after {time.perf_counter() - t0:.3f} s")
        raw.close()


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(8)
log(f"lab server on port {PORT}, {time.strftime('%Y-%m-%d %H:%M:%S')}")
n = 0
while True:
    c, a = srv.accept()
    n += 1
    threading.Thread(target=serve, args=(c, a, n), daemon=True).start()
