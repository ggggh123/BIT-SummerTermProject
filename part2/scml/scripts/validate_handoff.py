from __future__ import annotations

import csv
import hashlib
import json
import sys
from pathlib import Path


def count_rows(csv_path: Path) -> int:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        return sum(1 for _ in csv.DictReader(handle))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(handoff_dir: Path) -> list[str]:
    errors: list[str] = []
    manifest_path = handoff_dir / "manifest.json"
    injection_path = handoff_dir / "injection_log.json"

    if not manifest_path.exists():
        return [f"missing {manifest_path}"]
    if not injection_path.exists():
        errors.append(f"missing {injection_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for table, meta in manifest.get("tables", {}).items():
        table_path = handoff_dir / table / meta["path"]
        if not table_path.exists():
            errors.append(f"missing {table_path}")
            continue
        actual_rows = count_rows(table_path)
        actual_hash = sha256(table_path)
        if actual_rows != meta["rows"]:
            errors.append(f"{table}: rows {actual_rows} != manifest {meta['rows']}")
        if actual_hash != meta["sha256"]:
            errors.append(f"{table}: sha256 {actual_hash} != manifest {meta['sha256']}")

    if injection_path.exists():
        injection = json.loads(injection_path.read_text(encoding="utf-8"))
        issue_ids = {item.get("id") for item in injection.get("issues", [])}
        expected = {f"Q{i}" for i in range(1, 11)}
        if issue_ids != expected:
            errors.append(f"injection issue ids {sorted(issue_ids)} != {sorted(expected)}")

    return errors


def main(argv: list[str]) -> int:
    handoff_dir = Path(argv[1]) if len(argv) > 1 else Path("handoff/ods")
    errors = validate(handoff_dir)
    if errors:
        for error in errors:
            print(f"[FAIL] {error}")
        return 1
    print(f"[OK] {handoff_dir} manifest, hashes, row counts and injection log are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
