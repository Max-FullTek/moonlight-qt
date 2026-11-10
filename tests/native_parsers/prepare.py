"""Prepare only the SA-approved parser releases; never install system packages."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile
import urllib.request

RELEASES = (
    ("expat-2.10.0", "https://github.com/libexpat/libexpat/releases/download/R_2_10_0/expat-2.10.0.tar.gz",
     "5466a341f9c692ef7a958a6881311be597f8e1ce345984cf78414f54793dea4c"),
    ("json-3.12.0", "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz",
     "42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa"),
)


def prepare(root):
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, url, digest in RELEASES:
        archive = root / (name + (".tar.gz" if url.endswith(".gz") else ".tar.xz"))
        if not archive.exists():
            with urllib.request.urlopen(url, timeout=60) as response, archive.open("xb") as target:
                while chunk := response.read(1024 * 1024):
                    target.write(chunk)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Official release SHA256 mismatch: {archive.name}; nothing extracted")
        destination = root / name
        fresh = not destination.exists()
        if fresh:
            destination.mkdir()
        with tarfile.open(archive) as tar:
            for member in tar:
                # Releases have one top directory. Do not materialize links or
                # special files, and never follow a cached path outside root.
                parts = Path(member.name).parts
                if not parts or parts[0] not in (name, "json"):
                    raise RuntimeError("Unexpected release directory")
                target = destination.joinpath(*parts[1:]).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise RuntimeError("Archive path escaped dependency directory")
                if member.isdir():
                    if fresh:
                        target.mkdir(parents=True, exist_ok=True)
                    continue
                if not member.isfile():
                    raise RuntimeError("Links and special archive entries are not accepted")
                expected = tar.extractfile(member).read()
                if fresh:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open("xb") as output:
                        output.write(expected)
                elif not target.is_file() or hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(expected).digest():
                    raise RuntimeError(f"Cached dependency differs from verified release: {member.name}")
        result[name.split("-")[0]] = str(destination)
    print(json.dumps(result))


if __name__ == "__main__":
    prepare(Path(sys.argv[1]))
