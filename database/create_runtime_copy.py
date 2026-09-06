"""校验封存黄金库，并为一次冷启动创建全新的运行副本。"""
from __future__ import annotations

import argparse
import os
import re
import sqlite3
from pathlib import Path

from build_golden import _sha256

SHA256_RE = re.compile(r"[0-9a-f]{64}")


def _expected_hash(checksum_file):
    checksum_path = Path(checksum_file).resolve()
    if not checksum_path.is_file():
        raise FileNotFoundError(checksum_path)
    fields = checksum_path.read_text(encoding="utf-8").split()
    if not fields or SHA256_RE.fullmatch(fields[0]) is None:
        raise ValueError("checksum file must start with a lowercase SHA-256")
    return fields[0]


def _check_sqlite_integrity(path):
    uri = Path(path).resolve().as_uri() + "?mode=ro"
    conn = sqlite3.connect(uri, uri=True)
    try:
        result = conn.execute("PRAGMA integrity_check").fetchone()[0]
        if result != "ok":
            raise ValueError("golden database integrity_check failed: " + str(result))
        violations = conn.execute("PRAGMA foreign_key_check").fetchall()
        if violations:
            raise ValueError(
                "golden database foreign_key_check failed: " + repr(violations))
    finally:
        conn.close()


def create_runtime_copy(golden_db, checksum_file, output):
    """Return the verified digest after exclusively creating ``output``."""
    source = Path(golden_db).resolve()
    target = Path(output).resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    if source == target:
        raise ValueError("runtime output must differ from the golden database")

    target_sidecars = [Path(str(target) + suffix)
                       for suffix in ("-wal", "-shm", "-journal")]
    if os.path.lexists(target) or any(os.path.lexists(path)
                                     for path in target_sidecars):
        raise FileExistsError("refusing to overwrite an existing or open runtime database")

    source_sidecars = [Path(str(source) + suffix)
                       for suffix in ("-wal", "-shm", "-journal")]
    active_sidecars = [path for path in source_sidecars if os.path.lexists(path)]
    if active_sidecars:
        raise RuntimeError(
            "source database has active SQLite sidecar(s); stop the service "
            "and seal a checkpointed golden database first: "
            + ", ".join(str(path) for path in active_sidecars))

    expected = _expected_hash(checksum_file)
    actual = _sha256(source)
    if actual != expected:
        raise ValueError(
            f"checksum mismatch: expected {expected}, got {actual}")
    _check_sqlite_integrity(source)

    target.parent.mkdir(parents=True, exist_ok=True)
    fd = None
    created = False
    try:
        fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        created = True
        with source.open("rb") as src, os.fdopen(fd, "wb") as dst:
            fd = None
            for chunk in iter(lambda: src.read(1 << 20), b""):
                dst.write(chunk)
            dst.flush()
            os.fsync(dst.fileno())
        copied = _sha256(target)
        if copied != expected:
            raise RuntimeError(
                f"runtime copy checksum mismatch: expected {expected}, got {copied}")
        return copied
    except BaseException:
        if fd is not None:
            os.close(fd)
        if created:
            target.unlink(missing_ok=True)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify a sealed golden database and create a new runtime copy.")
    parser.add_argument("--golden-db", required=True)
    parser.add_argument("--checksum", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    digest = create_runtime_copy(args.golden_db, args.checksum, args.output)
    print(f"created runtime copy {args.output} sha256={digest}")


if __name__ == "__main__":
    main()
