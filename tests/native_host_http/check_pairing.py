"""Full production pairing over loopback HTTP + pinned mTLS, public keys only."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import sys
import threading
from urllib.parse import urlsplit


def main(executable, peer_executable, fixtures, work, extended=False):
    work.mkdir(parents=True, exist_ok=False)
    cases = [(6, "ok", 0), (7, "ok", 0), (7, "wrong-pin", 1), (7, "busy", 3),
             (7, "bad-certificate", 2), (7, "bad-signature", 2), (7, "short-challenge", 2),
             (7, "short-secret", 2), (7, "final-reject", 2)]
    cases += [(7, "nonblock-challenge", 2)] + [(7, f"reject-{stage}", 2) for stage in range(1, 6)]
    if extended:
        cases += [(7, f"http-error-{stage}", 2) for stage in range(1, 6)]
        cases += [(7, "invalid-xml", 2), (7, "cancel", 2)]
    for index, (generation, mode, expected) in enumerate(cases):
        seen = []
        with subprocess.Popen([peer_executable, "--peer", str(fixtures), str(generation), mode],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8") as peer:
            class Handler(http.server.BaseHTTPRequestHandler):
                protocol_version = "HTTP/1.1"

                def log_message(self, *_):
                    pass

                def do_GET(self):
                    url = urlsplit(self.path)
                    tls = isinstance(self.connection, ssl.SSLSocket)
                    seen.append((url.path, tls))
                    if mode == "cancel":
                        import time
                        time.sleep(0.3)
                        self.close_connection = True
                        return
                    if mode.startswith("http-error-") and len(seen) == int(mode.rsplit("-", 1)[1]):
                        self.send_response(503)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                    if mode == "invalid-xml":
                        body = b'<!DOCTYPE root><root status_code="200"/>'
                    elif url.path == "/unpair":
                        body = b'<root status_code="200"/>'
                    else:
                        assert url.path == "/pair", url.path
                        peer.stdin.write(f"{int(tls)} {url.query}\n")
                        peer.stdin.flush()
                        fields = peer.stdout.readline().split()
                        assert len(fields) == 5, fields
                        paired, certificate, name, challenge, secret = fields
                        name = "" if name == "-" else bytes.fromhex(name).decode("utf-8")
                        body = (f'<root status_code="200"><paired>{paired}</paired>'
                                f'<plaincert>{"" if certificate == "-" else certificate}</plaincert>'
                                f'<pairname>{name}</pairname>'
                                f'<challengeresponse>{"" if challenge == "-" else challenge}</challengeresponse>'
                                f'<pairingsecret>{"" if secret == "-" else secret}</pairingsecret></root>').encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)

            plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            secure = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
            context.verify_mode = ssl.CERT_REQUIRED
            context.load_verify_locations(fixtures / "server.pem")
            secure.socket = context.wrap_socket(secure.socket, server_side=True)
            threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (plain, secure)]
            for thread in threads:
                thread.start()
            try:
                env = dict(os.environ, NATIVE_HTTP_FIXTURE_HTTP_PORT=str(plain.server_port))
                env.pop("NATIVE_HTTP_WRONG_PIN", None)
                env.pop("RUST_PAIRING_EXPECTED_ERROR", None)
                if mode.startswith("http-error-") or mode in ("invalid-xml", "cancel"):
                    env["RUST_PAIRING_EXPECTED_ERROR"] = mode
                if mode == "wrong-pin":
                    env["NATIVE_HTTP_WRONG_PIN"] = "1"
                result = subprocess.run([executable, f"https://127.0.0.1:{secure.server_port}", str(fixtures / "server.pem"),
                                         str(fixtures), str(work / str(index)), f"pair-{generation}"],
                                        env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=8, check=True)
                values = {key: int(value) for key, value in (item.split("=") for item in result.stdout.split())}
                assert values == {"ok": 1, "error": expected, "status": 0}, (mode, values, result.stderr)
                if mode == "ok":
                    assert seen == [("/pair", False)] * 4 + [("/pair", True)], seen
                elif mode == "reject-1" or mode.startswith("http-error-") or mode in ("invalid-xml", "cancel"):
                    assert all(path != "/unpair" for path, _ in seen), seen
                else:
                    assert seen[-1] == ("/unpair", False) and seen.count(("/unpair", False)) == 1, seen
                if mode not in ("ok", "final-reject", "reject-5", "http-error-5"):
                    assert ("/pair", True) not in seen, seen
                print(f"production pairing generation={generation} mode={mode}: passed")
            finally:
                for server in (plain, secure):
                    server.shutdown()
                    server.server_close()
                for thread in threads:
                    thread.join(timeout=3)
                peer.stdin.close()
                peer.wait(timeout=3)
                assert peer.returncode == 0, peer.stderr.read()


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()),
         Path(sys.argv[3]).resolve(), Path(sys.argv[4]).resolve(), "--extended-rust" in sys.argv[5:])
