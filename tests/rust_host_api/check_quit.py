"""Pinned session cancellation and verification, production Rust vs NvHTTP."""
import http.server
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit


def main(executable, reference, qt_bin, work):
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    seen = []
    scenario = ""

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            path = urlsplit(self.path).path
            secure = isinstance(self.connection, ssl.SSLSocket)
            seen.append((path, secure))
            status = 200
            body = b'<root status_code="200"/>'
            if path == "/serverinfo":
                game = 42 if scenario == "still-running" else 0
                body = (f'<root status_code="200"><HttpsPort>{tls.server_port}</HttpsPort>'
                        f'<state>SUNSHINE_SERVER_BUSY</state><currentgame>{game}</currentgame></root>').encode()
                if scenario == "verify-xml-401" and secure:
                    body = b'<root status_code="401"/>'
                elif scenario == "verify-http-error":
                    status = 503
                elif scenario in ("cancel-verify", "verify-timeout"):
                    time.sleep(5.5 if scenario == "verify-timeout" else 0.6)
            elif path == "/cancel":
                if scenario == "xml-error":
                    body = b'<root status_code="599"/>'
                elif scenario == "host-message":
                    body = '<root status_code="503" status_message="主機拒絕 &amp; busy"/>'.encode()
                elif scenario == "http-error":
                    status = 503
                elif scenario == "redirect":
                    status = 302
                elif scenario == "invalid-xml":
                    body = b'<root status_code="200">'
                elif scenario in ("cancel-request", "slow-success"):
                    time.sleep(0.6)
            try:
                self.send_response(status)
                if status == 302:
                    self.send_header("Location", f"https://127.0.0.1:{tls.server_port}/target")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                pass

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    servers = (tls, plain)
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    for thread in threads:
        thread.start()

    def run(program, index, pin="server.pem", cancel=False):
        env = dict(os.environ, NATIVE_HTTP_FIXTURE_HTTP_PORT=str(plain.server_port),
                   HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
        env.pop("RUST_HOST_QUERY_CANCEL", None)
        if cancel:
            env["RUST_HOST_QUERY_CANCEL"] = "1"
        env["PATH"] = str(root / "libs/windows/lib/x64") + ";" + os.environ["SystemRoot"] + "/System32;" + os.environ["SystemRoot"]
        if program == reference:
            env["PATH"] = str(Path(qt_bin).resolve()) + ";" + env["PATH"]
        seen.clear()
        result = subprocess.run([str(Path(program).resolve()), f"https://127.0.0.1:{tls.server_port}/",
                                 str(fixtures / pin), str(fixtures), str(work / str(index)), "quit"],
                                env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=9, check=True)
        assert "PRIVATE KEY" not in result.stderr and "BEGIN CERTIFICATE" not in result.stderr
        assert all(secure or path == "/serverinfo" for path, secure in seen), seen
        assert sum(path == "/cancel" for path, _ in seen) <= 1, "Cancel retried"
        return result.stdout, list(seen)

    try:
        for scenario in ("success", "still-running", "xml-error", "http-error", "redirect", "slow-success", "verify-http-error", "verify-xml-401", "host-message"):
            native, native_seen = run(reference, "qt-" + scenario)
            rust, rust_seen = run(executable, "rust-" + scenario)
            result = json.loads(rust)
            expected = bool(int(re.search(r"ok=(\d+)", native)[1]))
            assert result["ok"] == expected, (scenario, native, result)
            assert native_seen == rust_seen, (scenario, native_seen, rust_seen)
            if scenario in ("still-running", "xml-error"):
                assert result["error"] in ("Xml(Status(599))", 'Rejected { code: 599, message: "" }'), result
            if scenario == "redirect":
                assert not any(path == "/target" for path, _ in rust_seen)
            if scenario == "host-message":
                assert result["error"] == 'Rejected { code: 503, message: "主機拒絕 & busy" }', result
            print("quit comparison:", scenario, "passed")
        for scenario, pin, cancel, expected in (
            ("success", "other.pem", False, "Transport(CertificateMismatch)"),
            ("invalid-xml", "server.pem", False, "Xml(Invalid)"),
            ("cancel-request", "server.pem", True, "Transport(Cancelled)"),
            ("cancel-verify", "server.pem", True, "Transport(Cancelled)"),
            ("verify-timeout", "server.pem", False, "Transport(Timeout)"),
        ):
            result, requests = run(executable, "rust-" + scenario + pin, pin, cancel)
            result = json.loads(result)
            assert result == {"ok": False, "error": expected}, (scenario, result)
            if pin == "other.pem":
                assert requests == [], requests
            if scenario == "invalid-xml":
                assert requests == [("/cancel", True)], requests
            print("quit failure:", scenario, "passed")
        print("14 session cancellation scenarios passed")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)


if __name__ == "__main__":
    main(*sys.argv[1:])
