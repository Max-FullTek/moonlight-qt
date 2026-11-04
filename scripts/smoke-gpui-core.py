"""Check the real worker's protocol and lifetime without a network host."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading


def check(executable, profile, eof=False, seeded=False):
    profile.mkdir(parents=True, exist_ok=False)
    if seeded:
        settings = profile / "Moonlight Game Streaming Project" / "MoonlightGPUI.ini"
        settings.parent.mkdir()
        settings.write_text(
            "[hosts]\nsize=1\n1\\hostname=CI Host\n1\\uuid=ci-offline-host\n"
            "1\\localaddress=127.0.0.1\n1\\localport=1\n"
            "1\\manualaddress=127.0.0.1\n1\\manualport=1\n",
            encoding="utf-8")
    env = dict(os.environ, MOONLIGHT_GPUI_PROFILE=str(profile.resolve()),
               QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
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
            while name != "hosts" and isinstance(value, dict) and value.get("event") == "hosts":
                value = received.get(timeout=30)
            assert isinstance(value, dict), value
            assert value.get("protocol") == 1, value
            assert value.get("event") == name, value
            return value

        def send(value):
            process.stdin.write(value + "\n")
            process.stdin.flush()

        try:
            assert event("ready")["qml_loaded"] is False
            hosts = event("hosts")["hosts"]
            if seeded:
                assert len(hosts) == 1 and hosts[0]["id"] == "ci-offline-host", hosts
                send('{"command":"rename","host":"ci-offline-host","name":""}')
                assert event("error")["code"] == "invalid_name"
                send(json.dumps({"command": "rename", "host": "ci-offline-host", "name": "測試 Host 😀"}))
                event("host_renamed")
                send('{"command":"list"}')
                assert event("hosts")["hosts"][0]["name"] == "測試 Host 😀"
                for command in ("quit", "displays", "start"):
                    send(json.dumps({"command": command, "host": "ci-offline-host"}))
                    assert event("error")["code"] == "not_ready"
                send('{"command":"remove","host":"ci-offline-host"}')
                assert event("host_removed")["host"] == "ci-offline-host"
                assert event("hosts")["hosts"] == []
                send('{"command":"shutdown"}')
            elif not eof:
                assert hosts == []
                send('{"command":"list"}')
                assert event("hosts")["hosts"] == []
                for request, code in [
                    ('[]', 'invalid_json'),
                    ('{"command":"unsupported"}', 'unknown_command'),
                    ('{"command":"start","host":"missing"}', 'host_not_found'),
                    ('{"command":"add","address":"https://invalid/"}', 'invalid_address'),
                    ('{"command":"app_preferences","host":"missing","app":1,"hidden":false,"direct_launch":true}', 'host_not_found'),
                    ('{"command":"rename","host":"missing","name":"Test"}', 'host_not_found'),
                    ('{"command":"remove","host":"missing"}', 'host_not_found'),
                    ('{"command":"quit","host":"missing"}', 'host_not_found'),
                    ('{"command":"displays","host":"missing"}', 'host_not_found'),
                    ('x' * (64 * 1024 + 1), 'request_too_large'),
                ]:
                    send(request)
                    assert event("error")["code"] == code
                send('{"command":"shutdown"}')
            process.stdin.close()
            assert process.wait(timeout=35) == 0
        finally:
            if process.poll() is None:
                process.kill()  # Only this script's known, isolated test worker.
                process.wait()


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve() / "Moonlight.exe"
    root = Path(sys.argv[2]).resolve()
    check(executable, root / "shutdown")
    check(executable, root / "parent-eof", eof=True)
    check(executable, root / "host-management", seeded=True)
    print("GPUI worker handshake, validation, offline host management, shutdown and parent EOF passed; no real host used.")
