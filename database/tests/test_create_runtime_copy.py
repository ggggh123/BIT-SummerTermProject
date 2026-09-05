import hashlib
import sqlite3

import pytest

from build_golden import build_base
from create_runtime_copy import create_runtime_copy

CUTOFF = "2026-09-01T09:00:00+08:00"


def _golden(tmp_path):
    out = tmp_path / "golden"
    build_base(out, seed=20260901, cutoff=CUTOFF, name="core.db")
    return out / "core.db", out / "core.db.sha256"


def test_create_runtime_copy_validates_and_copies_to_new_path(tmp_path):
    source, checksum = _golden(tmp_path)
    target = tmp_path / "runtime" / "round-001.db"

    digest = create_runtime_copy(source, checksum, target)

    assert target.is_file()
    assert digest == hashlib.sha256(source.read_bytes()).hexdigest()
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    conn = sqlite3.connect(str(target))
    assert conn.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    conn.close()

def test_create_runtime_copy_rejects_bad_hash_without_partial_output(tmp_path):
    source, _ = _golden(tmp_path)
    checksum = tmp_path / "bad.sha256"
    checksum.write_text("0" * 64 + "\n", encoding="utf-8")
    target = tmp_path / "runtime" / "round-001.db"

    with pytest.raises(ValueError, match="checksum mismatch"):
        create_runtime_copy(source, checksum, target)

    assert not target.exists()


def test_create_runtime_copy_refuses_open_existing_database(tmp_path):
    source, checksum = _golden(tmp_path)
    target = tmp_path / "runtime.db"
    conn = sqlite3.connect(str(target))
    conn.execute("CREATE TABLE marker(value TEXT NOT NULL)")
    conn.execute("INSERT INTO marker(value) VALUES ('keep-me')")
    conn.commit()

    with pytest.raises(FileExistsError, match="refusing to overwrite"):
        create_runtime_copy(source, checksum, target)

    assert conn.execute("SELECT value FROM marker").fetchone()[0] == "keep-me"
    conn.close()
