"""快速部署入口回归：不调用 apt、不真实编译或启动用户的演示轮次。"""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("quickstart_under_test", ROOT / "scripts/quickstart.py")
quickstart = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quickstart)


@pytest.fixture
def deployment(tmp_path, monkeypatch):
    monkeypatch.setattr(quickstart, "ROOT", tmp_path)
    monkeypatch.setattr(quickstart, "check_env", lambda: (True, "OK"))
    monkeypatch.setattr(os, "cpu_count", lambda: 64)
    for preset in ("ubuntu22", "ubuntu22-test"):
        for executable in ("apps/admin-server/ev_admin_server", "apps/user-client/ev_user_client",
                           "simulator/ev_charger_simulator"):
            target = tmp_path / "build" / preset / executable
            target.parent.mkdir(parents=True, exist_ok=True)
            target.touch()
    calls = []
    monkeypatch.setattr(quickstart, "configure_and_build", lambda preset, jobs: calls.append((preset, jobs)) or True)
    return calls


def test_default_build_is_production_only_with_two_jobs(deployment, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["quickstart.py", "--skip-install"])
    assert quickstart.main() == 0
    assert deployment == [("ubuntu22", "2")]


def test_full_regression_build_and_one_job_are_explicit(deployment, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["quickstart.py", "--preset", "ubuntu22-test", "--jobs", "1"])
    assert quickstart.main() == 0
    assert deployment == [("ubuntu22-test", "1")]


@pytest.mark.parametrize("jobs", ["0", "-1", "invalid"])
def test_invalid_parallelism_is_rejected_before_deployment(deployment, monkeypatch, jobs):
    monkeypatch.setattr(sys, "argv", ["quickstart.py", "--jobs", jobs])
    with pytest.raises(SystemExit) as error:
        quickstart.main()
    assert error.value.code == 2
    assert not deployment


def test_cmake_receives_explicit_parallelism(monkeypatch):
    calls = []
    monkeypatch.setattr(quickstart, "run", lambda cmd: calls.append(cmd) or 0)
    assert quickstart.configure_and_build("ubuntu22", "2")
    assert calls == [["cmake", "--preset", "ubuntu22"],
                     ["cmake", "--build", "--preset", "ubuntu22", "-j", "2"]]


def test_start_uses_unique_run_id_without_forcing_reset_of_existing_run(deployment, monkeypatch):
    runs = []
    monkeypatch.setattr(sys, "argv", ["quickstart.py", "--start"])
    monkeypatch.setattr(quickstart.time, "strftime", lambda _: "20260908-200000")
    monkeypatch.setattr(quickstart, "start_demo", lambda run_id, build: runs.append((run_id, build)) or True)
    assert quickstart.main() == 0
    assert quickstart.main() == 0
    assert runs[0][0] != runs[1][0]
    assert all(build == "build/ubuntu22" for _, build in runs)


def test_failed_environment_check_does_not_install_when_skipped(deployment, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["quickstart.py", "--skip-install"])
    monkeypatch.setattr(quickstart, "check_env", lambda: (False, "MISSING fixture"))
    monkeypatch.setattr(quickstart, "install_dependencies", lambda: pytest.fail("must not invoke apt"))
    assert quickstart.main() == 1
    assert not deployment


def test_shell_crlf_normalization_is_idempotent(tmp_path, monkeypatch):
    monkeypatch.setattr(quickstart, "ROOT", tmp_path)
    scripts = tmp_path / "scripts"
    scripts.mkdir()
    script = scripts / "check_env.sh"
    script.write_bytes(b"#!/bin/sh\r\nprintf 'fixture'\r\n")
    quickstart.normalize_shell_scripts()
    assert script.read_bytes() == b"#!/bin/sh\nprintf 'fixture'\n"
    quickstart.normalize_shell_scripts()
    assert script.read_bytes() == b"#!/bin/sh\nprintf 'fixture'\n"


@pytest.mark.parametrize("location", ["install.sh", "scripts/release/install.sh"])
def test_install_entry_works_from_another_directory_with_spaces(tmp_path, location):
    bundle = tmp_path / "source bundle"
    entry = bundle / location
    entry.parent.mkdir(parents=True)
    entry.write_bytes((ROOT / "scripts/release/install.sh").read_bytes())
    script = bundle / "scripts/quickstart.py"
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text("import pathlib, sys\nprint(pathlib.Path.cwd())\nprint(' '.join(sys.argv[1:]))\n")
    result = subprocess.run(["bash", str(entry), "--jobs", "1"], cwd=tmp_path,
                            capture_output=True, text=True, check=True)
    assert result.stdout.splitlines() == [str(bundle), "--start --jobs 1"]
