"""Actual C++ NvHTTP -> pipe RPC -> Rust owner vs original native HTTP.

Public loopback mTLS fixtures only. No decoder, game or user profile.
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


def main(reference, qt_bin, work):
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/clipboard_payload_routing/fixtures"
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    seen = []
    scenario = "normal"

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            self.do_GET()

        def do_GET(self):
            url = urlsplit(self.path)
            query = parse_qs(url.query)
            # Fresh nonce and hostname are supplied by production owners.
            query.pop("uuid", None)
            payload = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            seen.append((self.command, url.path, query, json.loads(payload) if payload else None,
                         isinstance(self.connection, ssl.SSLSocket), self.headers.get("Content-Type")))
            status = 200
            if url.path in ("/launch", "/resume"):
                body = '<root status_code="200"><sessionUrl0>rtsp://fixture/串流?x=1&amp;y=2</sessionUrl0></root>'.encode()
            elif url.path == "/serverinfo":
                body = (f'<root status_code="200"><HttpsPort>{tls.server_port}</HttpsPort>'
                        '<ServerCodecModeSupport>393217</ServerCodecModeSupport><appversion>7.1.0.0</appversion>'
                        '<GfeVersion>3.26.0.0</GfeVersion><state>SUNSHINE_SERVER_FREE</state></root>').encode()
            elif url.path.startswith("/api/"):
                body = json.dumps({"supported": True, "success": True, "hostMaxBitrate": 100000,
                                   "initialBitrate": 19000, "maxBitrate": 20000, "newBitrate": 17000,
                                   "reason": "調整"}, ensure_ascii=False).encode()
            else:
                body = b'<root status_code="200"/>'
            if scenario == "http-error":
                status = 503
            elif scenario == "redirect":
                status = 302
            elif scenario == "host-error" and url.path in ("/launch", "/resume"):
                body = '<root status_code="470" status_message="主機拒絕"/>'.encode()
            elif scenario == "bad-json":
                body = b'{"success":'
            elif scenario == "json-types":
                body = b'{"supported":"true","hostMaxBitrate":2147483648,"success":false,"initialBitrate":null}'
            elif scenario == "empty-info" and url.path == "/serverinfo":
                body = b'<root status_code="200"/>'
            elif scenario == "parallel":
                body = json.dumps({"success": True, "initialBitrate": 19000, "newBitrate": 17000, "reason": url.path}).encode()
                if url.path == "/api/abr/feedback":
                    time.sleep(0.3)
                else:
                    time.sleep(0.05)
            elif scenario == "owner-stop":
                time.sleep(0.5)
            elif scenario == "owner-error" and url.path == "/cancel":
                body = b'<root status_code="599"/>'
            try:
                self.send_response(status)
                if status == 302:
                    self.send_header("Location", f"https://127.0.0.1:{tls.server_port}/redirect-target")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except OSError:
                if scenario != "owner-stop":
                    raise

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "server.pem", fixtures / "server-key.pem")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(fixtures / "server.pem")
    tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    servers = (tls, plain)
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in servers]
    for thread in threads:
        thread.start()

    def run(mode, params, rpc, index, pin="server.pem"):
        input_path = work / f"input-{index}.json"
        input_path.write_text(json.dumps(params), encoding="utf8")
        args = [f"https://127.0.0.1:{tls.server_port}/", str(fixtures / pin), str(fixtures),
                str(work / f"profile-{index}"), "session-" + mode]
        env = dict(os.environ, SESSION_FIXTURE_INPUT=str(input_path),
                   NATIVE_HTTP_FIXTURE_HTTP_PORT=str(plain.server_port),
                   PATH=str(Path(qt_bin).resolve()) + ";" + str(root / "libs/windows/lib/x64") + ";" + os.environ["PATH"])
        env.pop("SESSION_FIXTURE_RPC", None)
        seen.clear()
        if rpc:
            result_path = work / f"result-{index}.json"
            env.update(SESSION_FIXTURE_PEER=str(Path(reference).resolve()), SESSION_FIXTURE_ARGS=json.dumps(args),
                       SESSION_FIXTURE_RESULT=str(result_path), SESSION_FIXTURE_STOP="1" if scenario == "owner-stop" else "0")
            command = ["cargo", "+1.99.1", "test", "--release", "--locked", "--manifest-path",
                       str(root / "gpui-prototype/Cargo.toml"), "host_api::tests::session_api_through_native_callers",
                       "--", "--exact", "--ignored"]
            result = subprocess.run(command, env=env, capture_output=True, text=True, encoding="utf8", errors="replace", timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            value = json.loads(result_path.read_text(encoding="utf8"))
        else:
            result = subprocess.run([str(Path(reference).resolve()), *args], env=env, capture_output=True,
                                    text=True, encoding="utf8", errors="replace", timeout=15, check=True)
            value = json.loads(result.stdout)
        return value, list(seen)

    cases = [("launch", "normal", {}), ("resume", "normal", {}),
             ("launch", "normal", {"fps": 120, "is_gfe": True}),
             ("launch", "normal", {"fps": 144}),
             ("resume", "normal", {"fps": 120, "is_gfe": True, "remote_fps": True, "remote_fps_rate": 165}),
             ("launch", "normal", {"width": 1280, "height": 720, "original_width": 3440, "original_height": 1440}),
             ("launch", "normal", {"original_width": 3440, "original_height": 1440, "remote_resolution": True, "remote_width": 2560}),
             ("launch", "normal", {"original_width": 3440}),
             ("launch", "normal", {"ten_bit": True, "hdr_mode": 1, "max_brightness": 1000.1234,
                                     "min_brightness": 0.0001234, "average_brightness": 456.7892, "sdr_white": 203.5,
                                     "screen_mode": 2, "use_vdd": True, "display": "螢幕 & + / ? 🎮"}),
             ("resume", "normal", {"use_vdd": False, "sdr_white": 1001}),
             ("launch", "host-error", {}), ("resume", "http-error", {}), ("launch", "redirect", {}),
             ("capabilities", "normal", {}), ("capabilities", "json-types", {}),
             ("configure", "normal", {"enabled": True}), ("configure", "normal", {"enabled": False}),
             ("feedback", "normal", {}), ("feedback", "bad-json", {}), ("configure", "http-error", {}),
             ("bitrate", "normal", {}), ("quit", "normal", {}),
             ("info", "normal", {}), ("info", "empty-info", {}),
             ("parallel", "parallel", {}), ("quit", "owner-error", {})]
    try:
        for index, (mode, scenario, params) in enumerate(cases):
            old, old_seen = run(mode, params, False, f"{index}-native")
            new, new_seen = run(mode, params, True, f"{index}-rust")
            assert old["ok"] == new["ok"], (mode, scenario, old, new)
            if old["ok"]:
                assert old["value"] == new["value"], (mode, scenario, old, new)
            elif scenario == "host-error":
                assert old["status"] == new["status"] == 470, (old, new)
            if scenario == "parallel":
                old_seen.sort(key=lambda entry: entry[1])
                new_seen.sort(key=lambda entry: entry[1])
            assert old_seen == new_seen, (index, old_seen, new_seen)
            assert all(entry[4] for entry in new_seen), "Control request downgraded"
            assert not any(path == "/redirect-target" for _, path, *_ in new_seen)
            print(f"session {index + 1}/{len(cases)} {mode}/{scenario}: matched", flush=True)
        # A dead Rust owner must wake a blocked caller, never fall back to
        # C++ network I/O or replay a mutation after the pipe disconnects.
        seen.clear()
        input_path = work / "disconnect-input.json"
        input_path.write_text("{}")
        env = dict(os.environ, SESSION_FIXTURE_RPC="1", SESSION_FIXTURE_INPUT=str(input_path),
                   NATIVE_HTTP_FIXTURE_HTTP_PORT=str(plain.server_port),
                   PATH=str(Path(qt_bin).resolve()) + ";" + str(root / "libs/windows/lib/x64") + ";" + os.environ["PATH"])
        process = subprocess.Popen([str(Path(reference).resolve()), f"https://127.0.0.1:{tls.server_port}/",
                                    str(fixtures / "server.pem"), str(fixtures), str(work / "disconnect-profile"), "session-launch"],
                                   env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding="utf8", errors="replace")
        try:
            request = json.loads(process.stdout.readline())
            assert request["event"] == "session_api"
            process.stdin.close()
            process.stdin = None
            stdout, stderr = process.communicate(timeout=5)
            assert process.returncode == 0
            result = json.loads(stdout)
            assert result["ok"] is False and seen == [], "Disconnected owner fell back to C++ HTTP"
            assert "PRIVATE KEY" not in stderr
        finally:
            if process.poll() is None:
                process.kill()  # Only this fixture's known child.
                process.wait()
        print(f"{len(cases)} native caller / Rust actor comparisons and in-flight EOF passed")
        scenario = "owner-stop"
        result, requests = run("launch", {}, True, "owner-stop")
        assert result["ok"] is False, "Stopped Rust actor reported launch success"
        assert len(requests) == 1 and requests[0][1] == "/launch", requests
        print("Stopping Rust owner during in-flight launch passed")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=3)


if __name__ == "__main__":
    main(*sys.argv[1:])
