"""Check the real worker's protocol and lifetime without a network host."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading


def check(executable, profile, eof=False, seeded=False, platform="offscreen",
          rename_only=False, reuse=False, rust_monitor=False):
    profile.mkdir(parents=True, exist_ok=reuse)
    if seeded and not reuse:
        settings = profile / "Moonlight Game Streaming Project" / "MoonlightGPUI.ini"
        settings.parent.mkdir()
        settings.write_text(
            "[hosts]\nsize=1\n1\\hostname=CI Host\n1\\uuid=ci-offline-host\n"
            "1\\localaddress=127.0.0.1\n1\\localport=1\n"
            "1\\manualaddress=127.0.0.1\n1\\manualport=1\n",
            encoding="utf-8")
    env = dict(os.environ, MOONLIGHT_GPUI_PROFILE=str(profile.resolve()),
               QT_QPA_PLATFORM=platform, QT_QUICK_BACKEND="software")
    if rust_monitor:
        # Match the shipping GPUI frontend: Rust owns host monitoring. This
        # isolates persistence from the legacy C++ network polling lifecycle.
        env["MOONLIGHT_GPUI_RUST_MONITOR"] = "1"
    if platform == "windows":
        # Verify the shipped platform without borrowing an SDK or caller's
        # Qt plugin overrides. The worker creates no streaming window here.
        for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"):
            env.pop(key, None)
        system_root = os.environ["SystemRoot"]
        env["PATH"] = os.pathsep.join((str(executable.parent),
                                      str(Path(system_root) / "System32"),
                                      system_root))
    with (profile / "stderr.log").open("wb") as log:
        process = subprocess.Popen([str(executable), "--gpui-core"], env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=log, text=True, encoding="utf-8")
        received = queue.Queue()

        def reader():
            try:
                for line in process.stdout:
                    received.put(json.loads(line))
            except Exception as error:
                received.put(error)
            finally:
                received.put(None)

        threading.Thread(target=reader, daemon=True).start()

        def event(name):
            value = received.get(timeout=30)
            # Polling may publish a host snapshot before a command completes.
            while isinstance(value, dict) and (value.get("event") == "monitor_contexts" or
                                               (name != "hosts" and value.get("event") == "hosts")):
                value = received.get(timeout=30)
            assert isinstance(value, dict), "Worker ended before the expected event"
            assert value.get("protocol") == 1, "Unexpected worker protocol"
            assert value.get("event") == name, ("Unexpected worker event", value.get("event"))
            return value

        def send(value):
            process.stdin.write(value + "\n")
            process.stdin.flush()

        try:
            assert event("ready")["qml_loaded"] is False
            hosts = event("hosts")["hosts"]
            if seeded:
                assert len(hosts) == 1 and hosts[0]["id"] == "ci-offline-host", hosts
                if reuse:
                    assert hosts[0]["name"] == "測試 Host 😀", hosts
                send('{"command":"rename","host":"ci-offline-host","name":""}')
                assert event("error")["code"] == "invalid_name"
                send(json.dumps({"command": "rename", "host": "ci-offline-host", "name": "測試 Host 😀"}))
                event("host_renamed")
                if not rename_only:
                    send('{"command":"list"}')
                    assert event("hosts")["hosts"][0]["name"] == "測試 Host 😀"
                    send('{"command":"host_api_context","host":"ci-offline-host","request_id":0}')
                    assert event("error")["code"] == "invalid_context_request"
                    send('{"command":"host_api_context","host":"ci-offline-host","request_id":1}')
                    assert event("error")["code"] == "not_ready"
                    for command in ("quit", "displays", "start"):
                        send(json.dumps({"command": command, "host": "ci-offline-host"}))
                        assert event("error")["code"] == "not_ready"
                    send('{"command":"remove","host":"ci-offline-host"}')
                    assert event("host_removed")["host"] == "ci-offline-host"
                    assert event("hosts")["hosts"] == []
                if not eof:
                    send('{"command":"shutdown"}')
            elif eof:
                assert hosts == [], hosts
            else:
                assert hosts == []
                send('{"command":"list"}')
                assert event("hosts")["hosts"] == []
                invalid_requests = [
                    ('[]', 'invalid_json'),
                    ('{"command":"unsupported"}', 'unknown_command'),
                    ('{"command":"controls","action":"open"}', 'controls_unavailable'),
                    ('{"command":"controls","action":"end"}', 'controls_unavailable'),
                    ('{"command":"controls","action":42}', 'invalid_controls'),
                    ('{"command":"controls","action":"bitrate","value":0}', 'invalid_controls'),
                    ('{"command":"controls","action":"bitrate","value":20000,"request_id":0}', 'invalid_controls'),
                    ('{"command":"controls","action":"bitrate","value":20000,"request_id":"one"}', 'invalid_controls'),
                    ('{"command":"start","host":"missing"}', 'host_not_found'),
                    ('{"command":"add","address":"https://invalid/"}', 'invalid_address'),
                    ('{"command":"app_preferences","host":"missing","app":1,"hidden":false,"direct_launch":true}', 'host_not_found'),
                    ('{"command":"rename","host":"missing","name":"Test"}', 'host_not_found'),
                    ('{"command":"remove","host":"missing"}', 'host_not_found'),
                    ('{"command":"quit","host":"missing"}', 'host_not_found'),
                    ('{"command":"displays","host":"missing"}', 'host_not_found'),
                    ('{"command":"host_api_context","host":"missing","request_id":1}', 'host_not_found'),
                    ('{"command":"pair_rust","host":"missing","request_id":1}', 'host_not_found'),
                    ('{"command":"pair_name","host":"missing","request_id":1,"name":"test"}', 'stale_pairing'),
                    ('{"command":"pair_complete","host":"missing","request_id":1,"state":0,"certificate":"invalid","error":""}', 'stale_pairing'),
                    ('x' * (64 * 1024 + 1), 'request_too_large'),
                ]
                # Submit a burst before reading replies to verify FIFO dispatch.
                for request, _ in invalid_requests:
                    send(request)
                for _, code in invalid_requests:
                    assert event("error")["code"] == code
                # A record above the pipe cap must be fully drained before
                # the next command can be dispatched.
                send("x" * (4 * 1024 * 1024 + 1))
                assert event("error")["code"] == "request_too_large"
                send('{"command":"list"}')
                assert event("hosts")["hosts"] == []
                send('{"command":"shutdown"}')
            process.stdin.close()
            assert process.wait(timeout=35) == 0
        finally:
            if process.poll() is None:
                process.kill()  # Only this script's known, isolated test worker.
                process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--platform", choices=("offscreen", "windows"), default="offscreen")
    args = parser.parse_args()
    executable = args.package.resolve() / "Moonlight.exe"
    root = args.profile.resolve()
    check(executable, root / "shutdown", platform=args.platform)
    check(executable, root / "parent-eof", eof=True, platform=args.platform)
    check(executable, root / "host-management", seeded=True, platform=args.platform)
    check(executable, root / "persistence", seeded=True, platform=args.platform,
          rename_only=True, rust_monitor=True)
    check(executable, root / "persistence", seeded=True, platform=args.platform,
          reuse=True, rust_monitor=True)
    check(executable, root / "persistence", eof=True, platform=args.platform,
          reuse=True, rust_monitor=True)
    check(executable, root / "persistence-eof", eof=True, seeded=True, platform=args.platform,
          rename_only=True, rust_monitor=True)
    check(executable, root / "persistence-eof", seeded=True, platform=args.platform,
          reuse=True, rust_monitor=True)
    print(f"GPUI worker handshake, validation, offline host management and persistence, shutdown and parent EOF passed ({args.platform}); no real host used.")
