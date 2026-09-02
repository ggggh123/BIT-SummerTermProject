"""Shared test helpers for the database test suite."""
import sqlite3
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


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
