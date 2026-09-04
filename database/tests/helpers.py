"""Shared test helpers for the database test suite."""
import hashlib
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# Make the ``database`` package (seed_demo.py etc.) importable from tests.
sys.path.insert(0, str(REPO_ROOT / "database"))


def _connect(path):
    conn = sqlite3.connect(str(path))
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def connect(db_path):
    """Open a connection with foreign-key enforcement enabled."""
    return _connect(db_path)


def apply_schema(db_path, schema_path):
    """Create the database by executing the schema.sql DDL."""
    conn = _connect(db_path)
    with open(schema_path, encoding="utf-8") as fh:
        conn.executescript(fh.read())
    conn.commit()
    conn.close()


def table_names(db_path):
    conn = _connect(db_path)
    rows = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table'").fetchall()
    conn.close()
    return {row[0] for row in rows}


def scalar(db_path, sql, params=()):
    conn = _connect(db_path)
    row = conn.execute(sql, params).fetchone()
    conn.close()
    return row[0] if row else None


def expect_integrity_error(db_path, sql, params=()):
    """Assert that executing ``sql`` raises sqlite3.IntegrityError."""
    conn = _connect(db_path)
    raised = False
    try:
        conn.execute(sql, params)
        conn.commit()
    except sqlite3.IntegrityError:
        raised = True
        conn.rollback()
    finally:
        conn.close()
    if not raised:
        raise AssertionError("expected sqlite3.IntegrityError for: " + sql)


def build_temp_db(tmp_path, seed=20260901, cutoff="2026-09-01T09:00:00+08:00",
                  name="demo.db"):
    """Apply the schema, seed the database, and return the SeedSummary."""
    from seed_demo import seed_database

    db = tmp_path / name
    apply_schema(db, Path("database/schema.sql"))
    conn = _connect(db)
    summary = seed_database(conn, seed=seed, cutoff=cutoff)
    conn.commit()
    conn.close()
    return summary


def canonical_hash(db_path):
    """Return a SHA-256 over every table's rows, in a stable order."""
    conn = _connect(db_path)
    tables = [r[0] for r in conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%' ORDER BY name").fetchall()]
    parts = []
    for table in tables:
        for row in conn.execute(f'SELECT * FROM "{table}" ORDER BY rowid'):
            parts.append(table + "|" + "|".join(repr(v) for v in row))
    conn.close()
    return hashlib.sha256("\n".join(parts).encode("utf-8")).hexdigest()
