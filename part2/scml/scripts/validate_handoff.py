from __future__ import annotations

import csv
import datetime as dt
import hashlib
import json
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "data_generator"))
from generator import PARTITION_COLUMNS, QUALITY_RULES  # noqa: E402


def count_rows(csv_path: Path) -> int:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        return sum(1 for _ in csv.DictReader(handle))


def count_file_rows(path: Path, file_format: str) -> int:
    if file_format == "csv":
        return count_rows(path)
    if file_format == "jsonl":
        return sum(1 for line in path.read_text(encoding="utf-8").splitlines() if line.strip())
    if file_format == "json":
        payload = json.loads(path.read_text(encoding="utf-8"))
        return len(payload.get("issues", [])) if isinstance(payload, dict) else 1
    raise ValueError(f"unsupported format: {file_format}")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_files(handoff_dir: Path, manifest: dict[str, object]) -> list[str]:
    errors: list[str] = []
    for meta in manifest.get("files", []):
        file_path = handoff_dir / str(meta["path"])
        if not file_path.exists():
            errors.append(f"missing {file_path}")
            continue
        actual_rows = count_file_rows(file_path, str(meta["format"]))
        actual_hash = sha256(file_path)
        if actual_rows != meta["rows"]:
            errors.append(f"{meta['path']}: rows {actual_rows} != manifest {meta['rows']}")
        if actual_hash != meta["sha256"]:
            errors.append(f"{meta['path']}: sha256 {actual_hash} != manifest {meta['sha256']}")
    return errors


def check_partitions(handoff_dir: Path, manifest: dict[str, object]) -> list[str]:
    errors: list[str] = []
    window = manifest.get("dataWindow") or {}
    start = end = None
    if window:
        start = dt.date.fromisoformat(str(window["start"])[:10])
        end = dt.date.fromisoformat(str(window["end"])[:10])

    for table, meta in (manifest.get("tables") or {}).items():
        table_meta = dict(meta)
        partitions = list(table_meta.get("partitions") or [])
        listed_files = list(table_meta.get("files") or [])

        if table in PARTITION_COLUMNS:
            if not partitions:
                errors.append(f"{table}: partitioned table has no dt= partitions")
                continue
            total = sum(int(item["rows"]) for item in partitions)
            if total != int(table_meta["rows"]):
                errors.append(f"{table}: partition rows {total} != table rows {table_meta['rows']}")
            if len(listed_files) != len(partitions):
                errors.append(f"{table}: {len(listed_files)} files != {len(partitions)} partitions")
            for item in partitions:
                name = str(item["dt"])
                try:
                    day = dt.date.fromisoformat(name)
                except ValueError:
                    errors.append(f"{table}: bad partition directory dt={name}")
                    continue
                if start and end and not (start <= day < end):
                    errors.append(f"{table}: partition dt={name} outside data window {start}..{end}")
                partition_dir = handoff_dir / table / f"dt={name}"
                if not (partition_dir / "_SUCCESS").exists():
                    errors.append(f"{table}: missing _SUCCESS in dt={name}")
        elif partitions:
            errors.append(f"{table}: snapshot table must not be dt partitioned")

    return errors


def check_injection(handoff_dir: Path) -> list[str]:
    errors: list[str] = []
    injection_path = handoff_dir / "injection_log.json"
    if not injection_path.exists():
        return [f"missing {injection_path}"]

    injection = json.loads(injection_path.read_text(encoding="utf-8"))
    issues = injection.get("issues", [])
    issue_ids = {item.get("rule") for item in issues}
    expected = set(QUALITY_RULES)
    if issue_ids != expected:
        errors.append(f"injection issue ids {sorted(issue_ids)} != {sorted(expected)}")

    covered: dict[str, set[str]] = {}
    for item in issues:
        if not item.get("row_id"):
            errors.append(f"injection issue {item.get('rule')} is missing row_id")
            continue
        covered.setdefault(str(item["rule"]), set()).add(str(item["table"]))

    for rule, spec in QUALITY_RULES.items():
        expected_tables = set(spec["tables"])
        actual_tables = covered.get(rule, set())
        if not actual_tables:
            errors.append(f"injection rule {rule} has no recorded rows")
            continue
        if actual_tables != expected_tables:
            errors.append(f"injection rule {rule} tables {sorted(actual_tables)} != {sorted(expected_tables)}")

    return errors


def validate(handoff_dir: Path) -> list[str]:
    manifest_path = handoff_dir / "manifest.json"
    injection_path = handoff_dir / "injection_log.json"

    if not manifest_path.exists():
        return [f"missing {manifest_path}"]

    errors: list[str] = []
    if not injection_path.exists():
        errors.append(f"missing {injection_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("contractVersion") != "ods-dwd-v0.1":
        errors.append("manifest contractVersion must be ods-dwd-v0.1")
    if manifest.get("kind") not in {"prl-test-fixture", "ods-handoff"}:
        errors.append("manifest kind must be prl-test-fixture or ods-handoff")

    errors.extend(check_files(handoff_dir, manifest))
    errors.extend(check_partitions(handoff_dir, manifest))
    errors.extend(check_injection(handoff_dir))

    return errors


def main(argv: list[str]) -> int:
    handoff_dir = Path(argv[1]) if len(argv) > 1 else Path("handoff/ods")
    errors = validate(handoff_dir)
    if errors:
        for error in errors:
            print(f"[FAIL] {error}")
        return 1
    print(f"[OK] {handoff_dir} manifest, hashes, row counts, dt partitions and injection log are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
