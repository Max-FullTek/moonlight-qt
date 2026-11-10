"""Real Rust Client -> C++ profile context -> Rust HTTPS -> mapped UI event.

Only loopback, public keys and a unique isolated profile. No visible UI/stream.
"""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
from urllib.parse import parse_qs, urlsplit


def main(worker, runtime_directory, qt_plugins, work):
    root = Path(__file__).resolve().parents[2]
    worker = Path(worker).resolve()
    runtime = Path(runtime_directory).resolve()
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    seen = []

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            seen.append((url.path, parse_qs(url.query)))
            if url.path == "/serverinfo":
                body = (f'<root status_code="200"><hostname>fixture</hostname><uniqueid>rust-display-host</uniqueid>'
                        f'<HttpsPort>{tls.server_port}</HttpsPort><PairStatus>1</PairStatus>'
                        '<state>SUNSHINE_SERVER_FREE</state><appversion>7.1.0.0</appversion>'
                        '<GfeVersion>3.26.0.0</GfeVersion></root>').encode()
            elif url.path == "/applist":
                body = b'<root status_code="200"><App><AppTitle>Desktop</AppTitle><ID>1</ID></App></root>'
            elif url.path == "/displays":
                body = json.dumps({"status_code": 200, "displays": [
                    {"friendly_name": "家中螢幕 🎮", "display_name": "screen", "device_id": "screen-1"}, {}]}, ensure_ascii=False).encode()
            else:
                body = b'<root status_code="404"/>'
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    profile = work / "core"
    settings = profile / "Moonlight Game Streaming Project/MoonlightGPUI.ini"
    settings.parent.mkdir(parents=True)

    def byte_array(path):
        return "@ByteArray(" + path.read_text(encoding="ascii").replace("\\", "\\\\").replace("\n", "\\n") + ")"

    settings.write_text(
        "[General]\ncertificate=" + byte_array(fixtures / "server.pem")
        + "\nkey=" + byte_array(fixtures / "server-key.pem") + "\nuniqueid=fixture-client\n"
        + "[hosts]\nsize=1\n1\\hostname=fixture\n1\\uuid=rust-display-host\n"
        + f"1\\manualaddress=127.0.0.1\n1\\manualport={plain.server_port}\n"
        + "1\\srvcert=" + byte_array(fixtures / "server.pem") + "\n"
        + "[Pairnames]\nrust-display-host=fixture-client-name\n", encoding="utf-8")
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (tls, plain)]
    for thread in threads:
        thread.start()
    env = dict(os.environ, RUST_HOST_API_WORKER=str(worker), RUST_HOST_API_PROFILE=str(profile),
               LOCALAPPDATA=str(work / "local"), QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(Path(qt_plugins).resolve()),
               QT_QPA_PLATFORM_PLUGIN_PATH=str(Path(qt_plugins).resolve() / "platforms"),
               PATH=str(runtime) + ";" + os.environ["PATH"])
    try:
        subprocess.run(["cargo", "+1.99.1", "test", "--release", "--locked", "--manifest-path",
                        str(root / "gpui-prototype/Cargo.toml"), "core::tests::rust_displays_through_real_worker_context",
                        "--", "--exact", "--ignored", "--nocapture"], env=env, check=True, timeout=90)
        displays = [entry for entry in seen if entry[0] == "/displays"]
        assert len(displays) == 1, seen
        query = displays[0][1]
        assert query["uniqueid"] == ["fixture-client"] and query["clientname"] == ["fixture-client-name"], query
        assert len(query["uuid"][0]) == 32, query
        log = (profile / "core-stderr.log").read_bytes()
        assert b"/displays" not in log, "C++ still requested displays"
        assert b"PRIVATE KEY" not in log and b"BEGIN CERTIFICATE" not in log, "Identity appeared in diagnostics"
        print("Real worker context, Rust mTLS display query, Chinese/fallback mapping and shutdown: passed")
    finally:
        for server in (tls, plain):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)
            assert not thread.is_alive()


if __name__ == "__main__":
    main(*sys.argv[1:])
