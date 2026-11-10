"""Production NvHTTP, isolated profile and public loopback identity only."""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
import time
import struct
import zlib
from urllib.parse import urlsplit, parse_qs


def main(executable, fixtures, work):
    work.mkdir(parents=True, exist_ok=False)
    seen = []
    scenario = ""

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            seen.append((url.path, parse_qs(url.query), getattr(self, "payload", None)))
            if scenario in ("slow", "cancel"):
                time.sleep(1.1)
            status = 503 if scenario == "failure" else 401 if scenario == "unauthorized" else 302 if scenario == "redirect" else 200
            if scenario == "image":
                def chunk(kind, data):
                    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
                body = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")) + chunk(b"IEND", b"")
            elif scenario == "malformed":
                body = b"not json"
            elif url.path == "/api/abr":
                body = b'{"ok":true}'
            elif scenario == "xml-401" and isinstance(self.connection, ssl.SSLSocket):
                body = b'<root status_code="401" status_message="Unpaired"/>'
            else:
                body = b'<root status_code="200"><currentgame>0</currentgame><sessionUrl0>rtsp://fixture/session</sessionUrl0></root>'
            try:
                self.send_response(status)
                if status == 302:
                    self.send_header("Location", f"https://127.0.0.1:{self.server.server_port}/target")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                pass

        def do_POST(self):
            data = self.rfile.read(int(self.headers["Content-Length"]))
            self.payload = (json.loads(data), self.headers.get("Content-Type"))
            self.do_GET()

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    plain_thread = threading.Thread(target=plain.serve_forever, daemon=True)
    thread.start()
    plain_thread.start()
    try:
        cases = [("serverinfo", "serverinfo", 0, 0), ("json", "json", 0, 0),
                 ("quit", "quit", 0, 0), ("launch", "launch", 0, 0), ("resume", "resume", 0, 0),
                 ("image", "image", 0, 0), ("failure", "failure", 403, 0),
                 ("unauthorized", "unauthorized", 204, 0), ("redirect", "redirect", 0, 302),
                 ("slow", "slow", 4, 0), ("cancel", "cancel", 4, 0),
                 ("malformed", "json", 0, -1), ("wrong-pin", "serverinfo", 6, 0),
                 ("xml-401", "serverinfo", 0, 0)]
        for index, (scenario, mode, error, status) in enumerate(cases):
            seen.clear()
            pin = fixtures / ("other.pem" if scenario == "wrong-pin" else "server.pem")
            env = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", NO_PROXY="", ALL_PROXY="http://127.0.0.1:1")
            env["NATIVE_HTTP_FIXTURE_HTTP_PORT"] = str(plain.server_port)
            result = subprocess.run([executable, f"https://127.0.0.1:{server.server_port}", str(pin),
                                     str(fixtures), str(work / str(index)), mode],
                                    env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=5, check=True)
            values = {key: int(value) for key, value in (item.split("=") for item in result.stdout.split())}
            assert values == {"ok": int(not error and not status), "error": error, "status": status}, (scenario, values, result.stderr)
            if scenario == "wrong-pin":
                assert not seen, seen
            elif scenario == "quit":
                assert [item[0] for item in seen] == ["/cancel", "/serverinfo"], seen
            elif scenario == "xml-401":
                assert [item[0] for item in seen] == ["/serverinfo", "/serverinfo"], seen
            else:
                assert len(seen) == 1, seen
            if mode in ("launch", "resume"):
                query = seen[0][1]
                assert query["appid"] == ["42"] and query["mode"] == ["1920x1080x60"] and query["display_name"] == ["桌面"] and query["corever"] == ["1"], query
            if scenario == "json":
                assert seen[0][2] == ({"enabled": True, "minBitrate": 1000, "maxBitrate": 20000, "mode": "繁中"}, "application/json"), seen
            if seen and mode != "json":
                assert seen[0][1]["uniqueid"] == ["fixture-client"] and seen[0][1]["uuid"], seen
            print(f"production NvHTTP {scenario}: passed")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
        plain.shutdown()
        plain.server_close()
        plain_thread.join(timeout=3)


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
