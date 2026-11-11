"""Real Rust Client -> C++ profile context -> Rust HTTPS -> mapped UI event.

Only loopback, fixture credentials and a unique isolated profile. The rejected
launch probes the local decoder but never starts a stream.
"""
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


def main(worker, runtime_directory, qt_plugins, work, peer_executable=None, mode="ok"):
    quit_mode = peer_executable == "quit"
    launch_mode = peer_executable == "launch-rejected"
    if quit_mode or launch_mode:
        peer_executable = None
    root = Path(__file__).resolve().parents[2]
    worker = Path(worker).resolve()
    runtime = Path(runtime_directory).resolve()
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    seen = []
    peer = None
    paired = not peer_executable
    pin_file = work / "pin.txt"
    phase_file = work / "quit-phase.txt"
    phase_file.write_text("reject")
    running = True

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            nonlocal peer, paired, running
            url = urlsplit(self.path)
            seen.append((url.path, parse_qs(url.query)))
            if url.path == "/serverinfo":
                if quit_mode and phase_file.read_text() == "restart":
                    running = True
                body = (f'<root status_code="200"><hostname>fixture</hostname><uniqueid>rust-display-host</uniqueid>'
                        f'<HttpsPort>{tls.server_port}</HttpsPort><PairStatus>{int(paired)}</PairStatus>'
                        f'<state>SUNSHINE_SERVER_{"BUSY" if quit_mode and running else "FREE"}</state>'
                        f'<currentgame>{42 if quit_mode and running else 0}</currentgame><appversion>7.1.0.0</appversion>'
                        '<GfeVersion>3.26.0.0</GfeVersion></root>').encode()
            elif url.path == "/pair":
                if peer is None:
                    deadline = time.monotonic() + 10
                    while not pin_file.exists() and time.monotonic() < deadline:
                        time.sleep(0.01)
                    pin = pin_file.read_text(encoding="ascii")
                    if mode == "wrong-pin":
                        pin = str((int(pin) + 1) % 10000).zfill(4)
                    peer = subprocess.Popen([peer_executable, "--peer", str(fixtures), "7", mode, pin],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            text=True, encoding="utf-8")
                secure = isinstance(self.connection, ssl.SSLSocket)
                peer.stdin.write(f"{int(secure)} {url.query}\n")
                peer.stdin.flush()
                fields = peer.stdout.readline().split()
                assert len(fields) == 5, fields
                accepted, cert, name, challenge, secret = fields
                decode = lambda value: "" if value == "-" else bytes.fromhex(value).decode("utf-8")
                body = (f'<root status_code="200"><paired>{accepted}</paired>'
                        f'<plaincert>{"" if cert == "-" else cert}</plaincert><pairname>{decode(name)}</pairname>'
                        f'<challengeresponse>{"" if challenge == "-" else challenge}</challengeresponse>'
                        f'<pairingsecret>{"" if secret == "-" else secret}</pairingsecret></root>').encode()
                if secure and accepted == "1":
                    paired = True
            elif url.path == "/unpair":
                body = b'<root status_code="200"/>'
            elif url.path == "/applist":
                body = b'<root status_code="200"><App><AppTitle>Desktop</AppTitle><ID>1</ID></App></root>'
            elif url.path == "/displays":
                body = json.dumps({"status_code": 200, "displays": [
                    {"friendly_name": "家中螢幕 🎮", "display_name": "screen", "device_id": "screen-1"}, {}]}, ensure_ascii=False).encode()
            elif url.path == "/launch" and launch_mode:
                assert isinstance(self.connection, ssl.SSLSocket), "Launch was not pinned TLS"
                body = b'<root status_code="503" status_message="fixture launch denied"/>'
            elif url.path == "/cancel" and quit_mode:
                assert isinstance(self.connection, ssl.SSLSocket), "Cancel was not pinned TLS"
                phase = phase_file.read_text()
                if phase == "hold":
                    phase_file.with_suffix(".started").write_text("started")
                time.sleep(1.0 if phase == "hold" else 0.4)
                if phase == "accept":
                    running = False
                body = b'<root status_code="200"/>'
            else:
                body = b'<root status_code="404"/>'
            try:
                self.send_response(503 if url.path == "/displays" and os.environ.get("RUST_DISPLAY_HTTP_ERROR") == "1" else 200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                if not quit_mode:
                    raise

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
        + "1\\srvcert=" + (byte_array(fixtures / "server.pem") if paired else "@ByteArray()") + "\n"
        + "[Pairnames]\nrust-display-host=fixture-client-name\n", encoding="utf-8")
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (tls, plain)]
    for thread in threads:
        thread.start()
    env = dict(os.environ, RUST_HOST_API_WORKER=str(worker), RUST_HOST_API_PROFILE=str(profile),
               RUST_PAIRING_PIN_FILE=str(pin_file), RUST_PAIRING_EXPECTED=("paired" if mode == "ok" else "pair_pin_mismatch" if mode == "wrong-pin" else "pair_failed"),
               RUST_QUIT_PHASE=str(phase_file),
               LOCALAPPDATA=str(work / "local"), QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(Path(qt_plugins).resolve()),
               QT_QPA_PLATFORM_PLUGIN_PATH=str(Path(qt_plugins).resolve() / "platforms"),
               PATH=str(runtime) + ";" + os.environ["PATH"])
    try:
        test = ("rust_rejected_launch_releases_real_worker_session" if launch_mode else
                "rust_quit_through_real_worker_context" if quit_mode else
                "rust_pairing_through_real_worker_context" if peer_executable else
                "rust_displays_through_real_worker_context")
        subprocess.run(["cargo", "+1.99.1", "test", "--release", "--locked", "--manifest-path",
                        str(root / "gpui-prototype/Cargo.toml"), f"core::tests::{test}",
                        "--", "--exact", "--ignored", "--nocapture"], env=env, check=True, timeout=90)
        if peer_executable:
            pairs = [entry for entry in seen if entry[0] == "/pair"]
            assert len(pairs) == (3 if mode == "wrong-pin" else 5), seen
            assert pairs[1][1]["clientname"] == ["測試主機"], pairs[1]
            assert len({entry[1]["uuid"][0] for entry in pairs}) == len(pairs), "Request nonce reused"
            saved = settings.read_text(encoding="utf-8")
            cert_line = next(line for line in saved.splitlines() if line.startswith("1\\srvcert="))
            assert ("-----BEGIN CERTIFICATE-----" in cert_line) == (mode == "ok"), "Incorrect certificate persistence"
            assert "rust-display-host=測試主機" in saved, "Pair name was not saved"
            assert len([entry for entry in seen if entry[0] == "/unpair"]) == (0 if mode == "ok" else 1), seen
        displays = [entry for entry in seen if entry[0] == "/displays"]
        if launch_mode:
            launches = [query for endpoint, query in seen if endpoint == "/launch"]
            assert len(launches) == 2, seen
            assert all(query["uniqueid"] == ["fixture-client"] for query in launches), launches
        elif quit_mode:
            cancellations = [entry for entry in seen if entry[0] == "/cancel"]
            assert len(cancellations) == 3, seen
            assert len({entry[1]["uuid"][0] for entry in cancellations}) == 3, "Cancel nonce reused"
            assert all(entry[1]["uniqueid"] == ["fixture-client"] for entry in cancellations)
            assert running  # The last operation was cancelled during shutdown.
            assert seen[-1][0] == "/cancel", "Shutdown continued into verification"
        elif not peer_executable:
            assert len(displays) == 1, seen
            query = displays[0][1]
            assert query["uniqueid"] == ["fixture-client"] and query["clientname"] == ["fixture-client-name"], query
            assert len(query["uuid"][0]) == 32, query
        log = (profile / "core-stderr.log").read_bytes()
        assert b"/displays" not in log, "C++ still requested displays"
        assert b"PRIVATE KEY" not in log and b"BEGIN CERTIFICATE" not in log, "Identity appeared in diagnostics"
        assert b"pair stage" not in log, "C++ still performed pairing"
        if quit_mode:
            assert b"Quit response:" not in log, "C++ still performed cancellation"
        operation = "launch rejection and retry" if launch_mode else "session quit" if quit_mode else "pairing " + mode if peer_executable else "display query"
        print(f"Real worker context, Rust {operation}, persistence and shutdown: passed")
    finally:
        for server in (tls, plain):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)
            assert not thread.is_alive()
        if peer is not None:
            peer.stdin.close()
            peer.wait(timeout=3)
            assert peer.returncode == 0, peer.stderr.read()


if __name__ == "__main__":
    main(*sys.argv[1:])
