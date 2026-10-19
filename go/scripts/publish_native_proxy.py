#!/usr/bin/env python3
"""Wrap prepared seekdb bundles as platform Go modules in a static proxy."""

import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / "native-release.json"


def put_immutable(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"refusing to replace published module file: {path}")
        return
    path.write_bytes(data)


def module_zip(module: str, version: str, files: dict[str, bytes], output: Path) -> None:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, contents in sorted(files.items()):
            entry = zipfile.ZipInfo(f"{module}@{version}/{name}")
            entry.date_time = (1980, 1, 1, 0, 0, 0)
            entry.compress_type = zipfile.ZIP_STORED if name.endswith(".tar.gz") else zipfile.ZIP_DEFLATED
            archive.writestr(entry, contents)
    if buffer.tell() > 500 * 1024 * 1024:
        raise ValueError(f"Go module zip exceeds 500 MiB: {output}")
    put_immutable(output, buffer.getvalue())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="directory to upload as the Go module proxy")
    parser.add_argument("bundle_root", type=Path, help="directory with <platform>/bundle.tar.gz")
    parser.add_argument(
        "--release-manifest",
        type=Path,
        default=RELEASE,
        help="versioned local release manifest",
    )
    parser.add_argument("--platform", choices=("darwin-arm64", "linux-amd64"), help="publish one platform")
    args = parser.parse_args()
    release = json.loads(args.release_manifest.read_text())
    version = release["module_version"]
    for platform in release["platforms"]:
        if args.platform and platform != args.platform:
            continue
        module = f'{release["module_base"]}/{platform}'
        bundle_path = args.bundle_root / platform / "bundle.tar.gz"
        print(f"reading {bundle_path}")
        bundle = bundle_path.read_bytes()
        bundle_sha256 = hashlib.sha256(bundle).hexdigest()

        template = ROOT / "native" / platform
        files = {
            path.name: path.read_bytes()
            for path in template.iterdir()
            if path.is_file() and (path.name in ("go.mod", "bundle.go", "LICENSE") or path.name.startswith("LICENSE."))
        }
        if files["go.mod"].decode().splitlines()[0] != f"module {module}":
            raise ValueError(f"{platform}: go.mod does not declare {module}")
        files["bundle.tar.gz"] = bundle

        version_dir = args.output / module / "@v"
        put_immutable(version_dir / f"{version}.mod", files["go.mod"])
        info = json.dumps({"Version": version, "Time": release["published_at"]}).encode() + b"\n"
        put_immutable(version_dir / f"{version}.info", info)
        module_zip(module, version, files, version_dir / f"{version}.zip")
        versions = set((version_dir / "list").read_text().splitlines()) if (version_dir / "list").exists() else set()
        versions.add(version)
        (version_dir / "list").write_text("\n".join(sorted(versions)) + "\n")
        print(f"published {module}@{version} (bundle SHA-256 {bundle_sha256})")


if __name__ == "__main__":
    main()
