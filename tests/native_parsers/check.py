"""Compare public XML/JSON compatibility cases; never contact a host or profile."""
import json
import os
from pathlib import Path
import subprocess
import sys


def cases():
    for name, body in [
        ("serverinfo", '<root status_code="200"><hostname>家用主機</hostname><currentgame>0</currentgame></root>'),
        ("pairing", '<root status_code="200"><paired>1</paired><plaincert>0100ff</plaincert></root>'),
        ("status-negative", '<root status_code="4294967295" status_message="錯誤 &amp; 測試"/>'),
        ("empty-title", '<root><App><AppTitle/><ID>1</ID><IsHdrSupported>1</IsHdrSupported></App></root>'),
        ("display-modes", '<root><DisplayMode><Width>1920</Width><Height>1080</Height><RefreshRate>60</RefreshRate></DisplayMode></root>'),
        ("numeric-entity", '<root><text>&#x1f3ae; &lt;&gt;&amp;&apos;&quot;</text></root>'),
        ("cdata-comment", '<root><text>a<![CDATA[<繁中>]]><!--ignore-->b</text></root>'),
        ("namespace", '<n:root xmlns:n="urn:host"><n:value n:attribute="yes">1</n:value></n:root>'),
        ("line-endings", '<root a="a\r\nb\tc">a\rb\r\nc</root>'),
        ("internal-entity", '<!DOCTYPE root [<!ENTITY value "繁中">]><root>&value;</root>'),
        ("undefined-entity", '<root>&unknown;</root>'),
        ("mismatched", '<root><item></root>'),
        ("duplicate-attribute", '<root a="1" a="2"/>'),
        ("trailing-data", '<root/>garbage'),
        ("empty", ''),
    ]:
        yield "xml", name, body.encode()
    yield "xml", "invalid-utf8", b'<root>\xff</root>'
    yield "xml", "entity-limit", b'<!DOCTYPE root [<!ENTITY value "' + b'x' * 4097 + b'">]><root>&value;</root>'
    yield "xml", "external-entity", b'<!DOCTYPE root [<!ENTITY value SYSTEM "file:///must-not-be-read">]><root>&value;</root>'
    for name, body in [
        ("host-json", '{"ok":true,"mode":"繁中 🎮","empty":null,"displays":[{"name":"桌面"}]}'),
        ("duplicate-key", '{"number":1,"number":2}'),
        ("integer", '{"number":2147483647}'),
        ("negative-integer", '{"number":-2147483648}'),
        ("overflow-integer", '{"number":2147483648}'),
        ("unsigned-integer", '{"number":18446744073709551615}'),
        ("exact-float", '{"number":1.0}'),
        ("fraction", '{"number":1.5}'),
        ("numeric-string", '{"number":"5"}'),
        ("exponent", '{"number":1e2}'),
        ("float-overflow", '{"number":1e400}'),
        ("array-root", '[]'),
        ("scalar-root", 'true'),
        ("comment", '{/* comment */"ok":true}'),
        ("trailing-comma", '{"ok":true,}'),
        ("bad-escape", r'{"text":"\q"}'),
        ("unpaired-surrogate", r'{"text":"\ud800"}'),
    ]:
        yield "json", name, body.encode()
    yield "json", "bom", b'\xef\xbb\xbf{"ok":true}'
    yield "json", "invalid-utf8", b'{"text":"\xff"}'
    yield "json", "candidate-depth-limit", b'{"data":' + b'[' * 128 + b'0' + b']' * 128 + b'}'
    yield "json", "deep-nesting", b'{"data":' + b'[' * 1100 + b'0' + b']' * 1100 + b'}'


def main(native, reference, output, qt_bin):
    entries = list(cases())
    requests = "".join(json.dumps({"kind": kind, "hex": body.hex()}) + "\n" for kind, _, body in entries)
    root = Path(__file__).resolve().parents[2]
    native_env = dict(os.environ, PATH=str(root / "libs/windows/lib/x64") + ";" + os.environ["SystemRoot"] + "/System32")
    def run(executable, env):
        result = subprocess.run([executable], input=requests, env=env, capture_output=True,
                                text=True, encoding="utf-8", errors="strict", timeout=20, check=True)
        values = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(values) == len(entries)
        return values
    native_values = run(native, native_env)
    reference_env = dict(native_env, PATH=qt_bin + ";" + native_env["PATH"])
    reference_values = run(reference, reference_env)
    findings = []
    def concise(value, depth=0):
        if isinstance(value, str) and len(value) > 256:
            return {"text_length": len(value), "prefix": value[:80]}
        if isinstance(value, list):
            return [concise(item, depth + 1) for item in value] if depth < 5 else {"array_length": len(value)}
        if isinstance(value, dict):
            return {key: concise(item, depth + 1) for key, item in value.items()} if depth < 5 else {"object_keys": list(value)}
        return value
    for (kind, name, _), candidate, existing in zip(entries, native_values, reference_values):
        equal = candidate == existing
        print(f"{kind}/{name}: {'match' if equal else 'DIFFERENCE'}")
        if not equal:
            findings.append({"kind": kind, "case": name, "candidate": concise(candidate), "qt": concise(existing)})
    # The reference may accept an unpaired UTF-16 surrogate. Keep it escaped
    # rather than attempting to encode that invalid scalar as UTF-8.
    Path(output).write_text(json.dumps({"cases": len(entries), "differences": findings}, ensure_ascii=True, indent=2), encoding="utf-8")
    print(f"Capability comparison: {len(entries)} cases, {len(findings)} differences; this command does not switch product parsers.")
    # Differences are evidence for SA; this is not a production acceptance pass.


if __name__ == "__main__":
    main(*sys.argv[1:])
