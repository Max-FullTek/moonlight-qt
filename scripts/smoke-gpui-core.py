"""Check the real worker's protocol and lifetime without a network host."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading


def check(executable, profile, eof=False):
    profile.mkdir(parents=True, exist_ok=False)
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
            assert isinstance(value, dict), value
            assert value.get("protocol") == 1, value
            assert value.get("event") == name, value
            return value

        def send(value):
            process.stdin.write(value + "\n")
            process.stdin.flush()

        try:
            assert event("ready")["qml_loaded"] is False
            assert event("hosts")["hosts"] == []
            if not eof:
                send('{"command":"list"}')
                assert event("hosts")["hosts"] == []
                for request, code in [
                    ('[]', 'invalid_json'),
                    ('{"command":"unsupported"}', 'unknown_command'),
                    ('{"command":"start","host":"missing"}', 'host_not_found'),
                    ('{"command":"add","address":"https://invalid/"}', 'invalid_address'),
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
    print("GPUI worker handshake, validation, shutdown and parent EOF passed; no host used.")
