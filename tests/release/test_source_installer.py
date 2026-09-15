"""源码包边界：独立临时目录验证，不读取或打包开发者的本机配置。"""
import hashlib
import importlib.util
from pathlib import Path
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("source_installer_under_test", ROOT / "scripts/release/make_installer.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


@pytest.fixture
def source(tmp_path, monkeypatch):
    root = tmp_path / "source tree"
    root.mkdir()
    monkeypatch.setattr(installer, "ROOT", root)
    monkeypatch.setattr(installer, "OUTPUT_DIR", root / "runtime/dist")
    monkeypatch.setattr(installer, "git_commit", lambda: "fixture-commit")
    files = {
        "CMakeLists.txt": b"project(Fixture)",
        "apps/user-client/main.cpp": b"int main() {}",
        ".github/workflows/ubuntu22.yml": b"name: fixture",
        ".gitignore": b"runtime/\n",
        "scripts/release/install.sh": (ROOT / "scripts/release/install.sh").read_bytes(),
        "runtime/golden/core.db": b"fixture core database",
        "runtime/golden/core.manifest.json": b'{"schemaVersion":1}',
        "runtime/golden/demo.db": b"fixture optional database",
        "runtime/golden/demo.db.sha256": b"optional fixture checksum",
        "runtime/golden/manifest.json": b"{}",
    }
    files["runtime/golden/core.db.sha256"] = hashlib.sha256(files["runtime/golden/core.db"]).hexdigest().encode()
    for name, data in files.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return root


def test_archive_keeps_sources_and_golden_artifacts_but_no_local_files(source):
    private_files = ["config.local.ini", ".env", ".env.local", "CMakeUserPresets.json",
                     ".git", ".superpowers/session.md", ".local-tools/scratch.py",
                     ".local-launcher-pulse/current.json", ".ssh/id_ed25519",
                     "build/cache.o", "build-release/cache.txt", "cmake-build-debug/cache.txt",
                     "runtime/demo-runs/current/core-runtime.db", "runtime/golden/unapproved.db",
                     "apps/user-client/config.local.ini", "personal.db", "local.log",
                     ".venv/bin/python", "old-release.tar.gz", "old-release.zip"]
    for name in private_files:
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("private fixture; must not ship")
    assert installer.main() == 0
    archive = next(installer.OUTPUT_DIR.glob("*.tar.gz"))
    with tarfile.open(archive) as bundle:
        names = {item.name.removeprefix("BIT-SummerTermProject/") for item in bundle.getmembers()}
        assert set(installer.GOLDEN_WHITELIST) <= names
        assert {"CMakeLists.txt", "apps/user-client/main.cpp", ".github/workflows/ubuntu22.yml",
                ".gitignore", "install.sh", "INSTALL-README.txt"} <= names
        assert not (set(private_files) & names)
        assert not any(item.issym() or item.islnk() for item in bundle.getmembers())


@pytest.mark.parametrize("missing", ["core.db", "core.db.sha256", "core.manifest.json"])
def test_missing_core_artifact_fails_before_creating_archive(source, missing):
    (source / "runtime/golden" / missing).unlink()
    with pytest.raises(ValueError, match="缺少核心黄金库工件"):
        installer.main()
    assert not installer.OUTPUT_DIR.exists()


def test_invalid_core_hash_fails_before_creating_archive(source):
    (source / "runtime/golden/core.db").write_bytes(b"changed fixture")
    with pytest.raises(ValueError, match="SHA-256"):
        installer.main()
    assert not installer.OUTPUT_DIR.exists()


def test_external_symlink_is_not_bundled(source, tmp_path):
    outside = tmp_path / "outside-file"
    outside.write_text("unrelated fixture")
    (source / "external.txt").symlink_to(outside)
    with pytest.raises(ValueError, match="符号链接"):
        installer.main()
    assert not installer.OUTPUT_DIR.exists()


def test_repeated_packaging_does_not_overwrite_an_existing_archive(source, monkeypatch):
    monkeypatch.setattr(installer.time, "strftime", lambda _: "20260908-200000")
    assert installer.main() == 0
    original = next(installer.OUTPUT_DIR.glob("*.tar.gz"))
    digest = hashlib.sha256(original.read_bytes()).hexdigest()
    assert installer.main() == 0
    assert len(list(installer.OUTPUT_DIR.glob("*.tar.gz"))) == 2
    assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
