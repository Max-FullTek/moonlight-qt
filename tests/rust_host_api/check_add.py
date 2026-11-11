"""Real GPUI Client and worker add/merge through Rust, isolated loopback only."""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit


def main(worker, runtime, plugins, work, mode="new"):
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    profile = work / "core"
    seen = []
    failures = 0

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            nonlocal failures
            url = urlsplit(self.path)
            secure = isinstance(self.connection, ssl.SSLSocket)
            seen.append((time.monotonic(), url.path, secure, parse_qs(url.query)))
            status = 200
            if url.path == "/serverinfo":
                body = (f'<root status_code="200"><hostname>新增主機 🎮</hostname><uniqueid>rust-add-host</uniqueid>'
                        f'<HttpsPort>{tls.server_port}</HttpsPort><PairStatus>{int(secure)}</PairStatus>'
                        '<LocalIP>127.0.0.1</LocalIP><appversion>7.1.0.0</appversion>'
                        '<GfeVersion>3.26.0.0</GfeVersion><state>SUNSHINE_SERVER_FREE</state></root>').encode()
                if mode in ("retry", "fail", "cancel") and not secure and failures < (1 if mode == "retry" else 2):
                    failures += 1
                    status = 503
            else:
                body = b'<root status_code="200"><App><AppTitle>Desktop</AppTitle><ID>42</ID></App></root>'
            self.send_response(status)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                pass  # Shutdown may cancel an in-flight monitor response.

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    settings = profile / "Moonlight Game Streaming Project/MoonlightGPUI.ini"
    settings.parent.mkdir(parents=True)

    def pem(name):
        return "@ByteArray(" + (fixtures / name).read_text().replace("\n", "\\n") + ")"

    seeded = mode in ("existing", "bad-pin")
    settings.write_text("[General]\ncertificate=" + pem("server.pem") + "\nkey=" + pem("server-key.pem") + "\nuniqueid=fixture-client\n"
                        + ("[hosts]\nsize=1\n1\\hostname=my-custom-name\n1\\customname=true\n1\\uuid=rust-add-host\n"
                           + "1\\manualaddress=127.0.0.1\n1\\manualport=1\n1\\srvcert=" + pem("other.pem" if mode == "bad-pin" else "server.pem")
                           + "\n1\\apps\\size=1\n1\\apps\\1\\id=42\n1\\apps\\1\\name=cached\n1\\apps\\1\\hidden=true\n" if seeded else ""), encoding="utf8")
    servers = (tls, plain)
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in servers]
    for thread in threads:
        thread.start()
    env = dict(os.environ, RUST_HOST_API_WORKER=str(Path(worker).resolve()), RUST_HOST_API_PROFILE=str(profile),
               RUST_ADD_ADDRESS=f"127.0.0.1:{plain.server_port}", RUST_ADD_MODE=mode,
               LOCALAPPDATA=str(work / "local"), QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(Path(plugins).resolve()),
               QT_QPA_PLATFORM_PLUGIN_PATH=str(Path(plugins).resolve() / "platforms"),
               PATH=str(Path(runtime).resolve()) + ";" + os.environ["PATH"])
    try:
        command = ["cargo", "+1.99.1", "test", "--release", "--locked", "--manifest-path",
                   str(root / "gpui-prototype/Cargo.toml"), "core::tests::rust_add_through_real_worker",
                   "--", "--exact", "--ignored", "--nocapture"]
        subprocess.run(command, env=env, check=True, timeout=45)
        requests = [item for item in seen if item[1] == "/serverinfo"]
        assert requests and not requests[0][2], seen
        assert requests[0][3]["uniqueid"] == ["0123456789ABCDEF"], requests[0]
        if mode == "existing":
            assert any(secure and query["uniqueid"] == ["fixture-client"] for _, _, secure, query in requests), seen
            identity_path = profile / "identity-v1.json"
            identity_bytes = identity_path.read_bytes()
            assert json.loads(identity_bytes)["unique_id"] == "fixture-client"
            old_profile = settings.read_text(encoding="utf8")
            assert "uniqueid=fixture-client" in old_profile
            settings.write_text(old_profile.replace("uniqueid=fixture-client", "uniqueid=fixture-legacy"), encoding="utf8")
            seen.clear()
            subprocess.run(command, env=env, check=True, timeout=45)
            assert any(secure and query["uniqueid"] == ["fixture-client"]
                       for _, path, secure, query in seen if path == "/serverinfo")
            assert identity_path.read_bytes() == identity_bytes
            assert "uniqueid=fixture-legacy" in settings.read_text(encoding="utf8")
        if mode == "bad-pin":
            assert not any(secure for _, _, secure, _ in requests), "Wrong pin reached HTTP"
            saved = settings.read_text(encoding="utf8")
            assert "1\\manualport=1\n" in saved and "my-custom-name" in saved, "Failed verification changed cached profile"
        if mode == "retry":
            assert requests[1][0] - requests[0][0] >= 4.9, "503 retry skipped delay"
        if mode == "fail":
            assert len(requests) == 2, "503 retried more than once"
        if mode == "cancel":
            assert len(requests) == 1, "Shutdown did not cancel retry wait"
        log = (profile / "core-stderr.log").read_bytes()
        assert b"Executing native request:" not in log and b"PRIVATE KEY" not in log and b"BEGIN CERTIFICATE" not in log
        print(f"Real worker Rust add / merge / pin / retry / shutdown: {mode} passed")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)


if __name__ == "__main__":
    main(*sys.argv[1:])
