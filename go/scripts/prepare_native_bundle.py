#!/usr/bin/env python3
"""Build a native bundle from a supplied seekdb binary and C SDK."""

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def set_linux_rpath(binary: Path) -> None:
    current = subprocess.run(
        ["patchelf", "--print-rpath", str(binary)], capture_output=True, text=True, check=True
    ).stdout.strip()
    if current != "$ORIGIN":
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(binary)], check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("darwin-arm64", "linux-amd64"))
    parser.add_argument("support_dir", type=Path, help="directory with libseekdb and its runtime libraries")
    parser.add_argument("output", type=Path, help="output bundle.tar.gz path")
    parser.add_argument("--seekdb-bin", type=Path, required=True, help="locally supplied seekdb executable")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="seekdb-release-") as temp_name:
        stage = Path(temp_name) / "stage"
        stage.mkdir()
        for support_file in args.support_dir.iterdir():
            if support_file.is_file() and support_file.name.startswith("lib"):
                shutil.copy2(support_file, stage / support_file.name)
        binary = stage / "seekdb"
        shutil.copy2(args.seekdb_bin, binary)
        binary.chmod(0o755)
        if args.platform == "linux-amd64":
            set_linux_rpath(binary)
        subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "package_native.py"),
             args.platform, str(stage), str(args.output)],
            check=True,
        )
    print(f"bundle SHA-256 {sha256(args.output)}: {args.output}")


if __name__ == "__main__":
    main()
