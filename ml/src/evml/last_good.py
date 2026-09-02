"""Atomic last-known-good forecast file handling."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


def save_last_good(path: str | Path, publish_payload: dict[str, Any]) -> None:
    """Atomically write the last-good forecast payload.

    The file is a replayable publish payload.  Write goes to a temp
    file, fsync, then os.replace for atomicity.  On failure, the
    pre-existing file remains unchanged.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(publish_payload, f, ensure_ascii=False, sort_keys=True, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def load_last_good(path: str | Path) -> dict[str, Any] | None:
    """Load the last-good file if it exists, else None."""
    path = Path(path)
    if not path.exists():
        return None
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)
