"""Production Rust host queries vs production NvHTTP/NvComputer, loopback only."""
import http.server
import json
import os
from pathlib import Path
import ssl
import socket
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit


def main(executable, reference, qt_bin, work):
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    seen = []
    body = b""
    scenario = ""

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def do_GET(self):
            secure = isinstance(self.connection, ssl.SSLSocket)
            url = urlsplit(self.path)
            seen.append((url.path, secure, parse_qs(url.query)))
            reply = body
            status = 200
            if scenario == "xml-401" and secure:
                reply = b'<root status_code="401"/>'
            elif scenario == "http-401":
                status = 401
            elif scenario == "http-error":
                status = 503
            elif scenario == "redirect":
                status = 302
            elif scenario == "slow":
                time.sleep(2.3)
            try:
                self.send_response(status)
                if status == 302:
                    self.send_header("Location", f"https://127.0.0.1:{secure_server.server_port}/target")
                self.send_header("Content-Length", str(len(reply)))
                self.end_headers()
                self.wfile.write(reply)
            except (OSError, ssl.SSLError):
                pass

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    secure_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    secure_server.socket = context.wrap_socket(secure_server.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    servers = (plain, secure_server)
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    for thread in threads:
        thread.start()

    def run(program, mode, index, pin="server.pem", cancel=False, hostname="127.0.0.1", http_port=None):
        env = dict(os.environ, NATIVE_HTTP_FIXTURE_HTTP_PORT=str(plain.server_port),
                   HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
        env.pop("RUST_HOST_QUERY_CANCEL", None)
        env.pop("RUST_HOST_QUERY_HTTP_URL", None)
        if http_port:
            env["NATIVE_HTTP_FIXTURE_HTTP_PORT"] = str(http_port)
            env["RUST_HOST_QUERY_HTTP_URL"] = f"http://[{hostname}]:{http_port}/"
        if cancel:
            env["RUST_HOST_QUERY_CANCEL"] = "1"
        # Rust/native SSL inputs are repository-local; Qt is reference-only.
        env["PATH"] = str(root / "libs/windows/lib/x64") + ";" + os.environ["SystemRoot"] + "/System32;" + os.environ["SystemRoot"]
        if program == reference:
            env["PATH"] = str(Path(qt_bin).resolve()) + ";" + env["PATH"]
        host = hostname if hostname == "127.0.0.1" else f"[{hostname}]"
        result = subprocess.run([str(Path(program).resolve()), f"https://{host}:{secure_server.server_port}/",
                                 str(fixtures / pin), str(fixtures), str(work / str(index)), mode],
                                env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=8, check=True)
        assert "PRIVATE KEY" not in result.stderr and "BEGIN CERTIFICATE" not in result.stderr
        return json.loads(result.stdout)

    try:
        server_fields = (
            '<hostname>家中主機 🎮</hostname><uniqueid>fixture-host</uniqueid>'
            '<mac>01:23:45:67:89:ab</mac><PairStatus>1</PairStatus><state>SUNSHINE_SERVER_BUSY</state>'
            '<currentgame>42</currentgame><ServerCodecModeSupport>393217</ServerCodecModeSupport>'
            '<MaxLumaPixelsHEVC>12345</MaxLumaPixelsHEVC><LocalIP>192.168.1.5</LocalIP><ExternalIP>example.test</ExternalIP>'
            '<appversion>7.1.0.0</appversion><GfeVersion>3.26.0.0</GfeVersion><gputype>測試 GPU</gputype>'
            '<DisplayMode><Width>1920</Width><Height>1080</Height><RefreshRate>60</RefreshRate></DisplayMode>'
            '<DisplayMode><Width>1280</Width><Height>720</Height><RefreshRate>120</RefreshRate></DisplayMode>')
        cases = [
            ("status-fields", "defaults", ""),
            ("status-fields", "complete", server_fields),
            ("status-fields", "nvidia-free", server_fields.replace("SUNSHINE_SERVER_BUSY", "MJOLNIR_SERVER_FREE")),
            ("status-fields", "ports", '<HttpsPort> +65535 </HttpsPort><ExternalPort>65536</ExternalPort><LocalIP>127.0.0.1</LocalIP>'),
            ("status-fields", "integers", '<ServerCodecModeSupport>2147483648</ServerCodecModeSupport><MaxLumaPixelsHEVC> -2147483648 </MaxLumaPixelsHEVC><state>X_SERVER_BUSY</state><currentgame>+42</currentgame>'),
            ("status-fields", "mac", '<mac>-1:+ff:0X10:80000000:7fffffff:0xgg:</mac>'),
            ("status-fields", "zero-mac", '<mac>00:00:00:00:00:00</mac>'),
            ("status-fields", "duplicates", '<hostname>first</hostname><hostname>second</hostname>'),
            ("status-fields", "namespace", '<x:hostname xmlns:x="urn:test"><![CDATA[繁中]]>&amp;&#x1f3ae;</x:hostname>'),
            ("app-fields", "empty", ""),
            ("app-fields", "apps", '<App><AppTitle>桌面 🎮</AppTitle><ID>42</ID><IsHdrSupported>1</IsHdrSupported><IsAppCollectorGame>1</IsAppCollectorGame></App><App><AppTitle/><ID>2</ID></App>'),
            ("app-fields", "negative-id", '<App><ID>-7</ID><AppTitle>negative</AppTitle><IsHdrSupported>true</IsHdrSupported></App>'),
            ("app-fields", "final-incomplete", '<App><ID>7</ID></App>'),
            ("app-fields", "earlier-incomplete", '<App><ID>7</ID></App><App><ID>2</ID><AppTitle>next</AppTitle></App>'),
            ("app-fields", "duplicate-id", '<App><ID>1</ID><AppTitle>first</AppTitle></App><App><ID>1</ID><AppTitle>second</AppTitle></App>'),
            ("app-fields", "duplicate-fields", '<App><ID>1</ID><ID>2</ID><AppTitle>first</AppTitle><AppTitle>second</AppTitle></App>'),
            ("app-fields", "nested-title", '<App><ID>1</ID><AppTitle>prefix<extra/>tail</AppTitle></App><App><ID>2</ID><AppTitle>next</AppTitle></App>'),
            ("status-fields", "nested-mode", '<DisplayMode><Width>12<extra/>34</Width><Height>10</Height></DisplayMode><DisplayMode><Width>1</Width></DisplayMode>'),
        ]
        for index, (mode, name, fields) in enumerate(cases):
            body = f'<root status_code="200">{fields}</root>'.encode()
            scenario = "normal"
            expected = run(reference, mode, f"qt-{index}")
            actual = run(executable, mode, f"rust-{index}")
            assert actual == expected, (name, expected, actual)
            print(f"host fields {mode}/{name}: match")

        body = (f'<root status_code="200"><HttpsPort>{secure_server.server_port}</HttpsPort>{server_fields}</root>').encode()
        for mode, scenario, expected_tls in [
            ("status-fields", "normal", [True]), ("status-discover", "normal", [False, True]),
            ("status-unpaired", "normal", [False]), ("status-fields", "xml-401", [True, False]),
        ]:
            seen.clear()
            expected = run(reference, mode, f"qt-policy-{mode}-{scenario}")
            assert [entry[1] for entry in seen] == expected_tls, seen
            seen.clear()
            actual = run(executable, mode, f"rust-policy-{mode}-{scenario}")
            assert actual == expected, (mode, scenario, expected, actual)
            assert [entry[1] for entry in seen] == expected_tls, seen
            assert all(query["uniqueid"] == ["fixture-client"] and len(query["uuid"][0]) == 32 for _, _, query in seen)
            assert len({query["uuid"][0] for _, _, query in seen}) == len(seen)
            print(f"host query {mode}/{scenario}: matched route and fields")

        for scenario, mode, pin, error in [
            ("normal", "status-fields", "other.pem", "CertificateMismatch"),
            ("normal", "status-discover", "other.pem", "CertificateMismatch"),
            ("http-401", "status-fields", "server.pem", "Http(401)"),
            ("http-error", "app-fields", "server.pem", "Http(503)"),
            ("redirect", "status-fields", "server.pem", "Redirect"),
            ("slow", "status-fields", "server.pem", "Timeout"),
            ("slow", "status-fields", "server.pem", "Cancelled"),
        ]:
            seen.clear()
            actual = run(executable, mode, f"error-{error}-{mode}", pin, error == "Cancelled")
            assert not actual["ok"] and error in actual["error"], (scenario, actual)
            assert not any(not tls for _, tls, _ in seen) or mode == "status-discover", "TLS failure downgraded"
            assert not any(path == "/target" for path, _, _ in seen), "Redirect followed"
            print(f"host query failure {mode}/{error}: passed")
        scenario = "normal"
        for index, body in enumerate([b'<root status_code="500"/>', b'<!DOCTYPE root><root status_code="200"/>',
                                      b'<root status_code="200">\xff</root>', b'<root status_code="200">']):
            result = run(executable, "status-fields", f"invalid-{index}")
            assert not result["ok"] and "Xml(" in result["error"], result
        print("host XML errors/approved strict policy: passed")
        class IPv6Server(http.server.ThreadingHTTPServer):
            address_family = socket.AF_INET6
        ipv6 = IPv6Server(("::1", 0), Handler)
        ipv6_thread = threading.Thread(target=ipv6.serve_forever, daemon=True)
        ipv6_thread.start()
        scenario, body = "normal", b'<root status_code="200"><uniqueid>ipv6</uniqueid></root>'
        try:
            for index, host in enumerate(("::1", "::1%251")):
                expected = run(reference, "status-unpaired", f"qt-ipv6-{index}", hostname=host, http_port=ipv6.server_port)
                actual = run(executable, "status-unpaired", f"rust-ipv6-{index}", hostname=host, http_port=ipv6.server_port)
                if host == "::1":
                    assert actual == expected and actual["ok"], (host, expected, actual)
                else:
                    # Existing native HTTP owner also rejects this scoped URL.
                    # Do not introduce a custom resolver/alias to change it.
                    assert not actual["ok"] and not expected["ok"], (host, expected, actual)
                print(f"host IPv6 {host}: matched existing outcome")
        finally:
            ipv6.shutdown()
            ipv6.server_close()
            ipv6_thread.join(timeout=3)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)
            assert not thread.is_alive()


if __name__ == "__main__":
    main(*sys.argv[1:])
