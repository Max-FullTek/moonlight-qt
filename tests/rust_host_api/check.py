"""Reuse public loopback TLS scenarios and the Qt parser corpus; no real host."""
import importlib.util
import json
import os
from pathlib import Path
import sys


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(executable, generator, reference, qt_bin, work):
    root = Path(__file__).resolve().parents[2]
    # The native generator needs only the existing OpenSSL DLLs. Qt is added
    # separately by the shared parser comparison, for its reference alone.
    os.environ["PATH"] = str(root / "libs/windows/lib/x64") + ";" + os.environ["SystemRoot"] + "/System32;" + os.environ["SystemRoot"]
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    transport = load("host_http_scenarios", root / "tests/native_host_http/check.py")
    transport.main(str(Path(executable).resolve()), root / "tests/clipboard_payload_routing/fixtures",
                   work / "tls", str(Path(generator).resolve()))
    parsers = load("parser_scenarios", root / "tests/native_parsers/check.py")
    report = work / "parser-comparison.json"
    parsers.main(str(Path(executable).resolve()), str(Path(reference).resolve()), str(report), str(Path(qt_bin).resolve()))
    findings = json.loads(report.read_text(encoding="utf-8"))
    # These standard-parser differences have SA approval. Keep the comparison
    # as a regression check; only displays is integrated in the product so far.
    expected = {("xml", "internal-entity"), ("xml", "external-entity"), ("xml", "invalid-utf8"),
                ("json", "unsigned-integer"), ("json", "bad-escape"), ("json", "unpaired-surrogate"),
                ("json", "candidate-depth-limit")}
    actual = {(item["kind"], item["case"]) for item in findings["differences"]}
    assert actual == expected, findings
    print("Shared production Rust transport passed; approved parser differences retained. No real host used.")


if __name__ == "__main__":
    main(*sys.argv[1:])
