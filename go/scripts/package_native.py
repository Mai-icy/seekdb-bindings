#!/usr/bin/env python3
"""Create a reproducible seekdb bundle for the central artifact store."""

import argparse
import gzip
import io
import subprocess
import tarfile
from pathlib import Path

PLATFORMS = {
    "darwin-arm64": ("libseekdb.dylib", "seekdb"),
    "linux-amd64": ("libseekdb.so", "seekdb"),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=PLATFORMS)
    parser.add_argument("native_dir", type=Path)
    parser.add_argument("output", type=Path, help="bundle.tar.gz path outside this repository")
    args = parser.parse_args()

    native_dir = args.native_dir.resolve(strict=True)
    library, executable = PLATFORMS[args.platform]
    required = {library, executable}
    files = sorted(
        path for path in native_dir.iterdir()
        if path.is_file() and (
            path.name in required
            or path.name.endswith((".dylib", ".so"))
            or ".so." in path.name
        )
    )
    missing = required - {path.name for path in files}
    if missing:
        parser.error(f"missing native files in {native_dir}: {', '.join(sorted(missing))}")

    if args.platform == "darwin-arm64":
        runtime = {"libssl.3.dylib", "libcrypto.3.dylib"}
        missing = runtime - {path.name for path in files}
        if missing:
            parser.error(f"missing bundled macOS runtime libraries: {', '.join(sorted(missing))}")
        try:
            result = subprocess.run(
                ["otool", "-L", str(native_dir / library)], capture_output=True, text=True
            )
        except FileNotFoundError:
            parser.error("otool is required to validate macOS shared libraries")
        if result.returncode != 0 or any(
            f"@loader_path/{name}" not in result.stdout for name in runtime
        ):
            parser.error(f"{library} must reference bundled OpenSSL through @loader_path")
    else:
        runtime = {"libssl.so.1.1", "libcrypto.so.1.1", "libaio.so.1"}
        missing = runtime - {path.name for path in files}
        if missing:
            parser.error(f"missing bundled Linux runtime libraries: {', '.join(sorted(missing))}")
        for path in files:
            try:
                result = subprocess.run(
                    ["patchelf", "--print-rpath", str(path)], capture_output=True, text=True
                )
            except FileNotFoundError:
                parser.error("patchelf is required to validate Linux runtime search paths")
            if result.returncode != 0 or "$ORIGIN" not in result.stdout.strip().split(":"):
                parser.error(f"{path} must have a $ORIGIN runtime search path")

    destination = args.output.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    launcher = b'#!/bin/sh\n"${0%/*}/seekdb.bin" "$@"\n'
    with destination.open("wb") as output, gzip.GzipFile(
        fileobj=output, mode="wb", filename="", mtime=0
    ) as compressed, tarfile.open(fileobj=compressed, mode="w") as archive:
        for path in files:
            data = path.read_bytes()
            name = "seekdb.bin" if path.name == executable else path.name
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if path.name == executable or path.name == library else 0o644
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            archive.addfile(info, io.BytesIO(data))
            print(f"included {name} ({len(data)} bytes)")
        info = tarfile.TarInfo(executable)
        info.size = len(launcher)
        info.mode = 0o755
        info.mtime = 0
        archive.addfile(info, io.BytesIO(launcher))
        print(f"included {executable} launcher ({len(launcher)} bytes)")
    print(f"wrote {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
