"""Isolated loopback TLS capability tests; only public test keys are used."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
import time


def main(executable, fixtures, work):
    work.mkdir(parents=True, exist_ok=False)
    renewed = work / "renewed.pem"
    subprocess.run([executable, "--renew-fixture", str(fixtures), str(renewed)], check=True)
    seen = []

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            seen.append((self.path, self.request_version))
            if self.path == "/slow":
                time.sleep(1.3)
            body = b"x" * 128 if self.path == "/large" else b"ok"
            try:
                self.send_response(302 if self.path == "/redirect" else 200)
                if self.path == "/redirect":
                    self.send_header("Location", f"https://127.0.0.1:{self.server.server_port}/target")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (OSError, ssl.SSLError):
                pass  # Expected after timeout/response-limit cancellation.

    def run(pin, route="/ok", identity=False, mutual=False, server_cert=None):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(server_cert or fixtures / "server.pem", fixtures / "server-key.pem")
        if mutual:
            context.verify_mode = ssl.CERT_REQUIRED
            context.load_verify_locations(fixtures / "server.pem")
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        seen.clear()
        thread.start()
        try:
            env = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
            result = subprocess.run([executable, f"https://127.0.0.1:{server.server_port}{route}",
                                     str(pin), str(fixtures), str(int(identity))],
                                    env=env, check=True, capture_output=True, text=True, timeout=10)
            values = dict(item.split("=") for item in result.stdout.split())
            return {key: int(value) for key, value in values.items()}, list(seen)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
            assert not thread.is_alive()

    original = fixtures / "server.pem"
    result, requests = run(original)
    assert result["code"] == 0 and result["status"] == 200 and result["verified"] == 1, result
    assert requests == [("/ok", "HTTP/1.1")], requests
    print("matching self-signed leaf, HTTP/1.1 and proxy bypass: passed")

    for label, pin, cert in [("different normally-trusted leaf", fixtures / "other.pem", None),
                             ("renewed certificate with the same public key", original, renewed)]:
        result, requests = run(pin, server_cert=cert)
        assert result["code"] != 0 and result["verified"] == 0 and requests == [], (result, requests)
        print(f"reject {label} before any HTTP request: passed")

    result, requests = run(original, mutual=True)
    assert result["code"] != 0 and requests == [], (result, requests)
    result, requests = run(original, identity=True, mutual=True)
    assert result["code"] == 0 and result["status"] == 200 and len(requests) == 1, (result, requests)
    print("mutual TLS requires and accepts explicit client identity: passed")

    result, requests = run(original, "/redirect")
    assert result["code"] == 0 and result["status"] == 302, result
    assert requests == [("/redirect", "HTTP/1.1")], requests
    print("redirect is returned without requesting its target: passed")

    result, requests = run(original, "/slow")
    assert result["code"] == 28 and len(requests) == 1, (result, requests)
    print("deadline cancels a stalled response: passed")
    result, requests = run(original, "/large")
    assert result["code"] == 23 and result["oversized"] == 1 and result["bytes"] <= 64, result
    print("response limit aborts excessive data: passed")
    print("All native host HTTP capability checks passed; no real host or user identity used.")


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
