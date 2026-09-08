"""环境入口的确定性回归，不要求测试宿主装齐可选 Web/ML。"""
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("environment_check", ROOT / "scripts/check_env.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


@pytest.fixture
def environment(tmp_path, monkeypatch):
    os_file = tmp_path / "os-release"
    os_file.write_text('ID=ubuntu\nVERSION_ID="22.04"\n')
    qt_root = tmp_path / "qt"
    for module in check.QT_MODULES:
        directory = qt_root / "cmake" / module
        directory.mkdir(parents=True)
        (directory / f"{module}Config.cmake").touch()
        (directory / f"{module}ConfigVersionImpl.cmake").write_text('set(PACKAGE_VERSION "6.2.4")')
    for relative in ("platforms/libqxcb.so", "sqldrivers/libqsqlite.so",
                     "iconengines/libqsvgicon.so", "tls/libqopensslbackend.so",
                     "QtWebEngineProcess", "resources/qtwebengine_resources.pak",
                     "qtwebengine_locales/en-US.pak"):
        target = qt_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.touch()
    calls = []
    overrides = {}

    def probe(args):
        calls.append(tuple(args))
        if tuple(args) in overrides:
            return overrides[tuple(args)]
        if args[0] == "qmake6":
            return 0, str(qt_root)
        return {("cmake", "--version"): (0, "cmake version 3.22.1"),
                ("g++", "-dumpfullversion"): (0, "11.4.0"),
                ("python3", "--version"): (0, "Python 3.10.12"),
                ("python3", "-c", "import pytest"): (0, ""),
                ("node", "--version"): (1, ""),
                ("python3", "-c", "import numpy, pandas, sklearn, joblib"): (1, "")}[tuple(args)]

    monkeypatch.setattr(check, "probe", probe)
    monkeypatch.setattr(check.shutil, "which", lambda name: "/usr/bin/" + name)
    return os_file, qt_root, calls, overrides


def failures(rows):
    return [message for status, message in rows if status == "MISSING"]


def test_jammy_core_passes_without_web_ml_or_charts(environment):
    os_file, _, calls, _ = environment
    assert not failures(check.inspect_environment(strict=True, os_release=os_file))
    assert not any("node" in cmd or "Qt6Charts" in cmd for cmd in calls)
    assert not any("numpy" in " ".join(cmd) for cmd in calls)


def test_newer_host_warns_but_strict_rejects(environment):
    os_file, qt_root, _, overrides = environment
    os_file.write_text('ID=ubuntu\nVERSION_ID="25.04"\n')
    for module in check.QT_MODULES:
        (qt_root / "cmake" / module / f"{module}ConfigVersionImpl.cmake").write_text('set(PACKAGE_VERSION "6.8.3")')
    rows = check.inspect_environment(os_release=os_file)
    assert not failures(rows)
    assert sum(status == "WARN" for status, _ in rows) == 2
    assert len(failures(check.inspect_environment(strict=True, os_release=os_file))) == 2


@pytest.mark.parametrize("relative", ["QtWebEngineProcess", "sqldrivers/libqsqlite.so",
                                      "iconengines/libqsvgicon.so", "resources/qtwebengine_resources.pak"])
def test_missing_runtime_resource_is_detected(environment, relative):
    os_file, qt_root, _, _ = environment
    (qt_root / relative).unlink()
    assert any(relative in value for value in failures(check.inspect_environment(os_release=os_file)))


@pytest.mark.parametrize("args,value", [
    (("cmake", "--version"), "cmake version 3.16.3"),
    (("g++", "-dumpfullversion"), "9.4.0"),
    (("python3", "--version"), "Python 3.8.10"),
])
def test_old_or_mixed_toolchain_is_rejected(environment, args, value):
    os_file, _, _, overrides = environment
    overrides[args] = (0, value)
    assert failures(check.inspect_environment(os_release=os_file))


@pytest.mark.parametrize("value", ["6.1.0", "6.8.3", ""])
def test_old_mixed_or_missing_qt_metadata_is_rejected(environment, value):
    os_file, qt_root, _, _ = environment
    (qt_root / "cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake").write_text(f'set(PACKAGE_VERSION "{value}")')
    assert failures(check.inspect_environment(os_release=os_file))


def test_optional_profiles_are_explicit(environment):
    os_file, _, calls, overrides = environment
    assert len(failures(check.inspect_environment(with_web=True, with_ml=True, os_release=os_file))) == 2
    overrides[("node", "--version")] = (0, "v18.20.0")
    overrides[("python3", "-c", "import numpy, pandas, sklearn, joblib")] = (0, "")
    assert not failures(check.inspect_environment(with_web=True, with_ml=True, os_release=os_file))
    assert ("node", "--version") in calls


def test_bootstrap_prints_complete_core_only_packages():
    result = subprocess.run(["sh", str(ROOT / "scripts/bootstrap.sh"), "--print-packages"],
                            text=True, capture_output=True, check=True)
    packages = set(result.stdout.split())
    assert {"build-essential", "libqt6opengl6-dev", "libqt6sql6-sqlite", "libqt6svg6",
            "qt6-webengine-dev-tools", "libqt6webenginecore6-bin", "libgl-dev", "libegl-dev"} <= packages
    assert not {"qt6-charts-dev", "nodejs", "npm", "python3-numpy"} & packages


@pytest.mark.parametrize("script", ["bootstrap.sh", "check_env.sh"])
def test_shell_entry_help_and_invalid_arguments(script):
    path = ROOT / "scripts" / script
    assert subprocess.run(["sh", "-n", str(path)]).returncode == 0
    assert subprocess.run(["sh", str(path), "--help"], capture_output=True).returncode == 0
    assert subprocess.run(["sh", str(path), "--unknown"], capture_output=True).returncode == 2


def test_presets_separate_quick_build_and_full_gate():
    data = json.loads((ROOT / "CMakePresets.json").read_text())
    configure = {p["name"]: p for p in data["configurePresets"]}
    assert data["version"] == 3
    assert configure["ubuntu22"]["generator"] == "Ninja"
    assert configure["ubuntu22"]["cacheVariables"]["BUILD_TESTING"] == "OFF"
    assert configure["ubuntu22-test"]["cacheVariables"]["BUILD_TESTING"] == "ON"
    assert configure["ubuntu22"]["binaryDir"] != configure["ubuntu22-test"]["binaryDir"]
    for preset in data["buildPresets"]:
        assert preset["jobs"] == 2
    gate = next(p for p in data["testPresets"] if p["name"] == "ubuntu22-test")
    assert gate["execution"] == {"jobs": 1, "noTestsAction": "error"}
