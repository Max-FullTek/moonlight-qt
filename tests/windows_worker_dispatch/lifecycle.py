"""Exercise the shared worker pipe with anonymous pipes and no Qt runtime."""

import json
import os
from pathlib import Path
import subprocess
import sys


def check(executable):
    system = Path(os.environ["SystemRoot"])
    env = dict(os.environ, PATH=os.pathsep.join((str(system / "System32"), str(system))))
    ready = {"event": "ready", "protocol": 1, "qml_loaded": False}
    oversize_error = {"event": "error", "protocol": 1, "code": "request_too_large"}
    shutdown = b'{"command":"shutdown"}\n'
    oversize = b" " * 65536 + shutdown
    owner_exit = b'{"command":"test_owner_exit"}\n'
    invalid = b"[]\n"
    monitor = b" " * 65536 + b'{"command":"monitor_update"}\n'
    monitor_event = {"event": "test_monitor_received", "protocol": 1}
    invalid_error = {"event": "error", "protocol": 1, "code": "invalid_json"}
    for records, expected in ((None, []), (shutdown, []), (oversize + shutdown, [oversize_error]),
                              (owner_exit, []), (invalid + monitor + shutdown, [invalid_error, monitor_event])):
        with subprocess.Popen([executable, "--gpui-core"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, env=env) as worker:
            if records is None:
                output, _ = worker.communicate(timeout=5)
                assert [json.loads(line) for line in output.splitlines()] == [ready]
            else:
                # Keep stdin open until the worker exits to distinguish shutdown
                # from termination caused by parent EOF.
                worker.stdin.write(records)
                worker.stdin.flush()
                try:
                    assert worker.wait(timeout=5) == 0
                    assert [json.loads(line) for line in worker.stdout] == [ready, *expected]
                except subprocess.TimeoutExpired:
                    worker.kill()
                    raise
            assert worker.returncode == 0


if __name__ == "__main__":
    check(sys.argv[1])
