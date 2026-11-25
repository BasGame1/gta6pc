#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
from pathlib import Path

BIN_NAME = "checker"

PROJECT_ROOT = Path(__file__).resolve().parent.parent
RUST_DIR = PROJECT_ROOT / "file_check"  # Change to "file_checker" if you renamed the folder
OUTPUT_DIR = PROJECT_ROOT / "bin"


def main():
    if not (RUST_DIR / "Cargo.toml").exists():
        print(f"Error: Cargo.toml not found in {RUST_DIR}", file=sys.stderr)
        sys.exit(1)

    print(f"Building release binary in {RUST_DIR}...")
    result = subprocess.run(["cargo", "build", "--release"], cwd=RUST_DIR)
    if result.returncode != 0:
        print("Build failed!", file=sys.stderr)
        sys.exit(result.returncode)

    exe_suffix = ".exe" if os.name == "nt" else ""
    built_binary = RUST_DIR / "target" / "release" / f"{BIN_NAME}{exe_suffix}"

    if not built_binary.exists():
        print(f"Error: Expected binary not found at {built_binary}", file=sys.stderr)
        sys.exit(1)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    dest_path = OUTPUT_DIR / built_binary.name

    if dest_path.exists():
        dest_path.unlink()

    shutil.move(str(built_binary), str(dest_path))
    print(f"Moved binary to: {dest_path}")

    print("Cleaning build artifacts...")
    subprocess.run(["cargo", "clean"], cwd=RUST_DIR, check=True)
    print("Done!")


if __name__ == "__main__":
    main()
