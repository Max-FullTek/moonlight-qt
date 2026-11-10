"""Real Client/worker polling, cache retention, address fallback and deletion."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit


def main(worker, runtime, plugins, work, large=None):
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    phase_file = work / "phase"
    phase_file.write_text("online")
    seen = []
    app_requests = 0
    app_times = []

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            nonlocal app_requests
            path = urlsplit(self.path).path
            phase = phase_file.read_text()
            secure = isinstance(self.connection, ssl.SSLSocket)
            wrong_host = self.server in (wrong, wrong_tls)
            seen.append((path, phase, secure, wrong_host, time.monotonic()))
            status = 200
            if phase == "offline":
                status, body = 503, b""
            elif path == "/serverinfo":
                uid = "wrong-host" if wrong_host else "rust-monitor-host"
                https_port = wrong_tls.server_port if wrong_host else tls.server_port
                body = (f'<root status_code="200"><uniqueid>{uid}</uniqueid><hostname>server-name</hostname>'
                        f'<HttpsPort>{https_port}</HttpsPort><PairStatus>1</PairStatus><state>SUNSHINE_SERVER_BUSY</state>'
                        '<currentgame>42</currentgame><appversion>7.1.0.0</appversion><mac>01:23:45:67:89:ab</mac></root>').encode()
            elif path == "/applist":
                app_requests += 1
                app_times.append(time.monotonic())
                if app_requests == 1:
                    status, body = 503, b""
                elif app_requests == 2:
                    body = b'<root status_code="200"/>'
                else:
                    extra = "" if not large else "".join(f'<App><ID>{1000+i}</ID><AppTitle>{"測試標題"*25}{i}</AppTitle></App>' for i in range(1200))
                    body = ('<root status_code="200"><App><ID>7</ID><AppTitle>隱藏應用</AppTitle></App>'
                            '<App><ID>99</ID><AppTitle/></App><App><ID>42</ID><AppTitle>桌面 🎮</AppTitle>'
                            f'<IsHdrSupported>1</IsHdrSupported></App>{extra}</root>').encode()
            else:
                status, body = 404, b""
            try:
                self.send_response(status)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (OSError, ssl.SSLError):
                pass

    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    wrong = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    wrong_tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    wrong_tls.socket = context.wrap_socket(wrong_tls.socket, server_side=True)
    profile = work / "core"
    settings = profile / "Moonlight Game Streaming Project/MoonlightGPUI.ini"
    settings.parent.mkdir(parents=True)

    def pem(name):
        return "@ByteArray(" + (fixtures / name).read_text().replace("\n", "\\n") + ")"

    settings.write_text("[General]\ncertificate=" + pem("server.pem") + "\nkey=" + pem("server-key.pem")
                        + "\nuniqueid=fixture-client\n[hosts]\nsize=1\n1\\hostname=custom-name\n1\\customname=true\n"
                        + f"1\\uuid=rust-monitor-host\n1\\localaddress=127.0.0.1\n1\\localport={wrong.server_port}\n"
                        + f"1\\manualaddress=127.0.0.1\n1\\manualport={plain.server_port}\n1\\srvcert=" + pem("server.pem")
                        + "\n1\\apps\\size=2\n1\\apps\\1\\id=42\n1\\apps\\1\\name=cached-desktop\n1\\apps\\1\\directlaunch=true\n"
                        + "1\\apps\\2\\id=7\n1\\apps\\2\\name=cached-hidden\n1\\apps\\2\\hidden=true\n", encoding="utf8")
    servers = (plain, wrong, tls, wrong_tls)
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    for thread in threads:
        thread.start()
    env = dict(os.environ, RUST_HOST_API_WORKER=str(Path(worker).resolve()), RUST_HOST_API_PROFILE=str(profile),
               RUST_MONITOR_APP_COUNT=str(1203 if large else 3), RUST_MONITOR_CHECK_INTERVAL="1" if large else "0",
               RUST_MONITOR_PHASE=str(phase_file), LOCALAPPDATA=str(work / "local"), QT_QPA_PLATFORM="offscreen",
               QT_PLUGIN_PATH=str(Path(plugins).resolve()), QT_QPA_PLATFORM_PLUGIN_PATH=str(Path(plugins).resolve() / "platforms"),
               PATH=str(Path(runtime).resolve()) + ";" + os.environ["PATH"])
    try:
        subprocess.run(["cargo", "+1.99.1", "test", "--release", "--locked", "--manifest-path", str(root / "gpui-prototype/Cargo.toml"),
                        "core::tests::rust_monitor_through_real_worker", "--", "--exact", "--ignored", "--nocapture"],
                       env=env, check=True, timeout=80)
        assert app_requests == (4 if large else 3), (app_requests, seen)
        if large:
            assert app_times[3] - app_times[2] >= 29.5, "App list refreshed before ten poll cycles"
        assert any(wrong_host and secure for _, _, secure, wrong_host, _ in seen), "Wrong-UUID TLS peer was not tested"
        failures = [entry for entry in seen if entry[1] == "offline"]
        assert len(failures) == 4, ("Online host did not retry both addresses twice", failures)
        assert not any(phase == "removed" for _, phase, _, _, _ in seen), "Deleted host kept polling"
        assert all(tls for path, _, tls, _, _ in seen if path == "/applist"), "Unpinned app query"
        log = (profile / "core-stderr.log").read_bytes()
        assert b"PRIVATE KEY" not in log and b"BEGIN CERTIFICATE" not in log
        assert "1\\uuid=rust-monitor-host" not in settings.read_text(encoding="utf8"), "Removed host was persisted again"
        print("Rust monitor, UUID/address fallback, app retries/order/preferences, offline/recovery, removal and shutdown: passed")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)
            assert not thread.is_alive()


if __name__ == "__main__":
    main(*sys.argv[1:])
