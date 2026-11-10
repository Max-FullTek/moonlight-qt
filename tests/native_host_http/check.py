"""Isolated loopback TLS capability tests; only public test keys are used."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
import time


def main(executable, fixtures, work, fixture_generator=None):
    work.mkdir(parents=True, exist_ok=False)
    renewed = work / "renewed.pem"
    subprocess.run([fixture_generator or executable, "--renew-fixture", str(fixtures), str(renewed)], check=True)
    seen = []
    posts = []
    arrived = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            seen.append((self.path, self.request_version))
            if self.path != "/partial":
                arrived.set()
            if self.path == "/slow":
                time.sleep(1.3)
            if self.path == "/partial":
                try:
                    self.send_response(200)
                    self.send_header("Content-Length", "256")
                    self.end_headers()
                    self.wfile.write(b"ok")
                    self.wfile.flush()
                    arrived.set()
                    time.sleep(1.3)
                    self.wfile.write(b"x" * 254)
                except (OSError, ssl.SSLError):
                    pass
                return
            body = b"x" * 128 if self.path == "/large" else b"ok"
            try:
                self.send_response(302 if self.path == "/redirect" else 503 if self.path == "/failure" else 200)
                if self.path == "/redirect":
                    self.send_header("Location", f"https://127.0.0.1:{self.server.server_port}/target")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (OSError, ssl.SSLError):
                pass  # Expected after timeout/response-limit cancellation.

        def do_POST(self):
            posts.append((self.rfile.read(int(self.headers["Content-Length"])), self.headers.get("Content-Type")))
            self.do_GET()

    def run(pin, route="/ok", identity=False, mutual=False, server_cert=None, mode="", tls=True, host="127.0.0.1", version=None):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        if version:
            context.minimum_version = version
            context.maximum_version = version
        context.load_cert_chain(server_cert or fixtures / "server.pem", fixtures / "server-key.pem")
        if mutual:
            context.verify_mode = ssl.CERT_REQUIRED
            context.load_verify_locations(fixtures / "server.pem")
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        if tls:
            server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        seen.clear()
        posts.clear()
        arrived.clear()
        thread.start()
        try:
            env = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
            args = [executable, f"{'https' if tls else 'http'}://{host}:{server.server_port}{route}",
                    str(pin), str(fixtures), str(int(identity)), mode]
            if mode == "cancel":
                with subprocess.Popen(args, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True) as process:
                    assert arrived.wait(3), "request did not reach local fixture"
                    start = time.monotonic()
                    stdout, stderr = process.communicate(input="\n", timeout=3)
                    elapsed = time.monotonic() - start
                    assert elapsed < 0.6, f"cancellation did not wake event wait: {elapsed}"
                    result = subprocess.CompletedProcess(args, process.returncode, stdout, stderr)
                    result.check_returncode()
            else:
                result = subprocess.run(args, env=env, check=True, capture_output=True, text=True, timeout=10)
            assert not result.stderr.strip(), result.stderr
            values = dict(item.split("=") for item in result.stdout.split())
            return {key: int(value) for key, value in values.items()}, list(seen)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
            assert not thread.is_alive()

    original = fixtures / "server.pem"
    if len(sys.argv) > 4 and sys.argv[4] == "event-owner":
        cases = [
            ("/ok", "", original, False, False, 0, 200, 1),
            ("/ok", "", fixtures / "other.pem", False, False, 7, 0, 0),
            ("/ok", "", original, False, True, 3, 0, 0),
            ("/ok", "", original, True, True, 0, 200, 1),
            ("/redirect", "", original, False, False, 0, 302, 1),
            ("/slow", "", original, False, False, 4, 0, 1),
            ("/large", "", original, False, False, 6, 200, 1),
            ("/ok", "exact-limit", original, False, False, 0, 200, 1),
            ("/failure", "", original, False, False, 0, 503, 1),
            ("/ok", "post", original, False, False, 0, 200, 1),
            ("/ok", "replace-pin", original, False, False, 7, 0, 1),
            ("/ok", "remove-identity", original, True, True, 3, 0, 1),
            ("/ok", "invalid-pin", original, False, False, 1, 0, 0),
            ("/slow", "owner-cancel", original, False, False, 5, 0, 1),
            ("/partial", "owner-cancel", original, False, False, 5, 200, 1),
            ("/partial", "", original, False, False, 4, 200, 1),
        ]
        for route, mode, pin, identity, mutual, error, status, count in cases:
            start = time.monotonic()
            result, requests = run(pin, route, identity, mutual, mode="event-" + mode)
            assert result["error"] == error and result["status"] == status and len(requests) == count, (mode, route, result, requests)
            if error:
                assert result["bytes"] == 0, result
            if mode == "post":
                assert posts == [('{"message":"繁中"}'.encode("utf-8"), "application/json")], posts
            if mode == "owner-cancel":
                # Includes the server's bounded shutdown wait, not just the request.
                assert time.monotonic() - start < 1.2, "owner shutdown did not cancel an unlimited request"
            print(f"event owner {route} {mode or 'GET'} identity={identity} mutual={mutual}: passed")
        result, requests = run(original, server_cert=renewed, mode="event-")
        assert result["error"] == 7 and not requests, (result, requests)
        result, requests = run(original, tls=False, mode="event-unpaired")
        assert result["error"] == 0 and result["verified"] == 0 and len(requests) == 1, (result, requests)
        print("event owner renewed leaf rejection and explicit unpaired HTTP: passed")
        result, requests = run(original, host="localhost", mode="event-")
        assert result["error"] == 0 and result["verified"] == 1 and len(requests) == 1, (result, requests)
        print("event owner hostname resolution and localhost address selection: passed")
        return
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
    assert result["error"] == 4 and len(requests) == 1, (result, requests)
    print("deadline cancels a stalled response: passed")
    result, requests = run(original, "/large")
    assert result["error"] == 6 and result["oversized"] == 1 and result["bytes"] == 0, result
    print("response limit aborts excessive data: passed")

    result, requests = run(original, mode="exact-limit")
    assert result["error"] == 0 and result["bytes"] == 2, result
    print("response exactly at size limit is retained: passed")

    result, requests = run(original, "/failure")
    assert result["error"] == 0 and result["status"] == 503 and result["bytes"] == 2, result
    print("HTTP failure status and body remain available to protocol policy: passed")

    for mode in ("no-pin", "invalid-pin", "partial-identity"):
        result, requests = run(original, identity=True, mode=mode)
        assert result["error"] == 1 and requests == [], (mode, result, requests)
    print("missing/malformed pin and partial identity rejected before network I/O: passed")

    result, requests = run(original, mode="post")
    assert result["error"] == 0 and posts == [('{"message":"繁中"}'.encode("utf-8"), "application/json")], posts
    print("UTF-8 JSON POST reaches the server unchanged: passed")

    result, requests = run(original, route="/slow", mode="cancel")
    assert result["cancelled"] == 1 and result["bytes"] == 0 and len(requests) == 1, (result, requests)
    result, requests = run(original, mode="pre-cancel")
    assert result["cancelled"] == 1 and requests == [], (result, requests)
    print("pre-cancel and cross-thread cancellation wakeup: passed")

    result, requests = run(original, mode="replace-pin")
    assert result["error"] == 7 and len(requests) == 1, (result, requests)
    result, requests = run(original, identity=True, mutual=True, mode="remove-identity")
    assert result["error"] != 0 and len(requests) == 1, (result, requests)
    print("sequential requests do not reuse old pin or client identity: passed")

    result, requests = run(original, tls=False)
    assert result["error"] == 1 and requests == [], (result, requests)
    result, requests = run(original, tls=False, mode="unpaired-credentials", identity=True)
    assert result["error"] == 1 and requests == [], (result, requests)
    result, requests = run(original, tls=False, mode="unpaired")
    assert result["error"] == 0 and result["verified"] == 0 and len(requests) == 1, (result, requests)
    print("unpaired HTTP requires explicit opt-in and carries no TLS identity: passed")

    for version in (ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3):
        result, requests = run(original, identity=True, mutual=True, version=version)
        assert result["error"] == 0 and result["verified"] == 1 and len(requests) == 1, (version, result, requests)
    print("pinned mutual TLS 1.2 and 1.3: passed")
    for mode, error in (("", 4), ("cancel", 5)):
        result, requests = run(original, route="/partial", mode=mode)
        assert result["error"] == error and result["status"] == 200 and result["bytes"] == 0 and len(requests) == 1, (mode, result, requests)
    print("timeout and cancellation discard partial body: passed")
    result, requests = run(original, host="localhost")
    assert result["error"] == 0 and result["verified"] == 1 and len(requests) == 1, (result, requests)
    print("hostname resolution with pinned leaf: passed")
    print("All native host HTTP capability checks passed; no real host or user identity used.")


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
