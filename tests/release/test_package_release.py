"""Ubuntu 22.04 portable release collector behavior tests."""
import configparser
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import uuid

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "release"))

import package_release as package_release_module
from package_release import (
    collect_elf_dependencies,
    package_release,
    prepare_output,
    read_needed,
    write_sanitized_config,
)


def test_package_refuses_existing_output(tmp_path):
    output = tmp_path / "existing"
    output.mkdir()

    with pytest.raises(FileExistsError):
        prepare_output(output)


def test_prepare_output_supports_spaces_and_creates_one_new_directory(tmp_path):
    output = tmp_path / "portable output with spaces"

    assert prepare_output(output) == output.resolve()
    assert output.is_dir()


def test_config_copy_keeps_only_local_connection_and_tencent_key(tmp_path, capsys):
    source = tmp_path / "authorized fixture.ini"
    source.write_text(
        "[server]\n"
        "host=remote.example\n"
        "port=65535\n"
        "password=must-not-copy\n"
        "[tencent]\n"
        "mapKey=fixture-%-key#value\n"
        "otherCredential=must-not-copy-either\n",
        encoding="utf-8",
    )
    destination = tmp_path / "bundle" / "config.local.ini"

    write_sanitized_config(source, destination)

    assert destination.read_text(encoding="utf-8") == (
        "[server]\n"
        "host = 127.0.0.1\n"
        "port = 9100\n\n"
        "[tencent]\n"
        "mapKey = fixture-%-key#value\n"
    )
    assert capsys.readouterr() == ("", "")


@pytest.mark.parametrize(
    "content",
    ["[server]\nhost=127.0.0.1\n", "[tencent]\nmapKey=   \n", "not ini"],
)
def test_config_copy_rejects_missing_blank_or_malformed_map_key(tmp_path, content):
    source = tmp_path / "fixture.ini"
    source.write_text(content, encoding="utf-8")

    with pytest.raises(ValueError, match="mapKey|INI"):
        write_sanitized_config(source, tmp_path / "config.local.ini")


@pytest.mark.skipif(not Path("/bin/sh").is_file(), reason="Linux ELF fixture unavailable")
def test_read_needed_uses_elf_dynamic_section():
    assert "libc.so.6" in read_needed(Path("/bin/sh"))


def test_dependency_collection_recurses_inside_sysroot_and_preserves_aliases(
        tmp_path, monkeypatch):
    sysroot = tmp_path / "Jammy root"
    library_dir = sysroot / "usr/lib/x86_64-linux-gnu"
    library_dir.mkdir(parents=True)
    (library_dir / "libfirst.so.1.2").write_bytes(b"first-real")
    (library_dir / "libfirst.so.1").symlink_to("libfirst.so.1.2")
    (library_dir / "libsecond.so.2").write_bytes(b"second-real")
    seed = tmp_path / "program with spaces"
    seed.write_bytes(b"program")
    needed = {
        seed: ["libfirst.so.1", "libc.so.6", "ld-linux-x86-64.so.2"],
        library_dir / "libfirst.so.1.2": ["libsecond.so.2"],
        library_dir / "libsecond.so.2": [],
    }
    monkeypatch.setattr(
        package_release_module, "read_needed", lambda path: needed[Path(path)]
    )
    destination = tmp_path / "bundle with spaces" / "lib"

    sources = collect_elf_dependencies(sysroot, [seed], destination)

    assert sources == {
        library_dir / "libfirst.so.1.2",
        library_dir / "libsecond.so.2",
    }
    assert (destination / "libfirst.so.1").is_symlink()
    assert os.readlink(destination / "libfirst.so.1") == "libfirst.so.1.2"
    assert (destination / "libfirst.so.1.2").read_bytes() == b"first-real"
    assert (destination / "libsecond.so.2").read_bytes() == b"second-real"
    assert not (destination / "libc.so.6").exists()
    assert not (destination / "ld-linux-x86-64.so.2").exists()


def test_dependency_collection_fails_when_non_system_library_is_missing(
        tmp_path, monkeypatch):
    sysroot = tmp_path / "sysroot"
    sysroot.mkdir()
    seed = tmp_path / "app"
    seed.write_bytes(b"program")
    monkeypatch.setattr(
        package_release_module, "read_needed", lambda _path: ["libmissing.so.4"]
    )

    with pytest.raises(FileNotFoundError, match="libmissing.so.4"):
        collect_elf_dependencies(sysroot, [seed], tmp_path / "lib")


def _copy_elf(destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2("/bin/true", destination)


def _make_package_inputs(tmp_path):
    sysroot = tmp_path / "Jammy root with spaces"
    build = tmp_path / "Release build with spaces"
    source = tmp_path / "source whitelist root"
    output = tmp_path / "EVCharging fixture package"
    config = tmp_path / "authorized fixture.ini"

    programs = {
        "apps/admin-server/ev_admin_server",
        "simulator/ev_charger_simulator",
        "apps/user-client/ev_user_client",
    }
    for relative in programs:
        _copy_elf(build / relative)

    plugin_root = sysroot / "usr/lib/x86_64-linux-gnu/qt6/plugins"
    for relative in (
        "platforms/libqxcb.so",
        "platforms/libqoffscreen.so",
        "sqldrivers/libqsqlite.so",
        "imageformats/libqjpeg.so",
        "tls/libqopensslbackend.so",
        "xcbglintegrations/libqxcb-glx-integration.so",
    ):
        _copy_elf(plugin_root / relative)
    _copy_elf(sysroot / "usr/lib/qt6/libexec/QtWebEngineProcess")
    for name in (
        "qtwebengine_resources.pak",
        "qtwebengine_resources_100p.pak",
        "qtwebengine_resources_200p.pak",
        "qtwebengine_devtools_resources.pak",
    ):
        path = sysroot / "usr/share/qt6/resources" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(("resource:" + name).encode())
    for name in ("en-US.pak", "zh-CN.pak"):
        path = sysroot / "usr/share/qt6/translations/qtwebengine_locales" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(("locale:" + name).encode())

    _copy_elf(sysroot / "usr/bin/python3.10")
    stdlib = sysroot / "usr/lib/python3.10"
    (stdlib / "encodings").mkdir(parents=True)
    (stdlib / "encodings/__init__.py").write_text("# fixture\n", encoding="utf-8")
    (stdlib / "os.py").write_text("# fixture\n", encoding="utf-8")
    (stdlib / "__pycache__").mkdir()
    (stdlib / "__pycache__/os.cpython-310.pyc").write_bytes(b"must not ship")
    _copy_elf(stdlib / "lib-dynload/_sqlite3.cpython-310-x86_64-linux-gnu.so")
    library_root = sysroot / "usr/lib/x86_64-linux-gnu"
    _copy_elf(library_root / "libssl.so.3")
    _copy_elf(library_root / "libcrypto.so.3")

    font = sysroot / "usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    font.parent.mkdir(parents=True)
    font.write_bytes(b"fixture font")

    for relative in (
        "scripts/release/portable_launcher.py",
        "scripts/release/portable_runtime.py",
        "scripts/demo_runtime.py",
        "scripts/demo_processes.py",
        "scripts/demo_protocol.py",
        "database/create_runtime_copy.py",
        "database/build_golden.py",
        "database/seed_demo.py",
        "database/schema.sql",
        "docs/release/portable-release.md",
        "runtime/golden/core.db",
        "runtime/golden/core.db.sha256",
        "runtime/golden/core.manifest.json",
    ):
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture:" + relative + "\n", encoding="utf-8")
    (source / ".git").mkdir()
    (source / ".git/config").write_text("must not ship", encoding="utf-8")
    (source / "config.local.ini").write_text("must not read or ship", encoding="utf-8")
    (source / "private-notes.txt").write_text("must not ship", encoding="utf-8")

    copyright_path = sysroot / "usr/share/doc/fixture-runtime/copyright"
    copyright_path.parent.mkdir(parents=True)
    copyright_path.write_text(
        "Fixture redistribution notice; see /usr/share/common-licenses/GPL-3\n",
        encoding="utf-8",
    )
    common_license = sysroot / "usr/share/common-licenses/GPL-3"
    common_license.parent.mkdir(parents=True)
    common_license.write_text("Fixture GPL-3 full text\n", encoding="utf-8")
    (common_license.parent / "GPL").symlink_to("GPL-3")
    packaged_paths = [
        path for path in sysroot.rglob("*")
        if path.is_file() or path.is_symlink()
    ]
    info = sysroot / "var/lib/dpkg/info"
    info.mkdir(parents=True)
    (info / "fixture-runtime.list").write_text(
        "\n".join("/" + str(path.relative_to(sysroot)) for path in packaged_paths) + "\n",
        encoding="utf-8",
    )
    status = sysroot / "var/lib/dpkg/status"
    status.parent.mkdir(parents=True, exist_ok=True)
    status.write_text(
        "Package: fixture-runtime\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 6.2.4-fixture\n\n",
        encoding="utf-8",
    )
    config.write_text(
        "[server]\nhost=do-not-copy.invalid\nport=1\n"
        "[tencent]\nmapKey=fake-test-map-key\n",
        encoding="utf-8",
    )
    return sysroot, build, source, output, config


def _run_package(inputs):
    sysroot, build, source, output, config = inputs
    return package_release(
        sysroot=sysroot,
        build_dir=build,
        source_dir=source,
        output_dir=output,
        config_file=config,
        release_id="1.0.0-20260907-3aa9a27",
        source_commit="3aa9a2721d493584a40640d9a0147d802d9723e6",
    )


@pytest.mark.parametrize("existing_kind", ["file", "directory", "symlink", "dangling"])
def test_package_entrypoint_rejects_every_existing_output_identity(
        tmp_path, existing_kind):
    inputs = _make_package_inputs(tmp_path)
    output = inputs[3]
    if existing_kind == "file":
        output.write_text("user file", encoding="utf-8")
    elif existing_kind == "directory":
        output.mkdir()
    else:
        target = tmp_path / (
            "valid symlink target" if existing_kind == "symlink" else "missing target"
        )
        if existing_kind == "symlink":
            target.mkdir()
        output.symlink_to(target, target_is_directory=True)
    original_inode = os.lstat(output).st_ino

    with pytest.raises(FileExistsError, match="overwrite"):
        _run_package(inputs)

    assert os.path.lexists(output)
    assert os.lstat(output).st_ino == original_inode
    if existing_kind == "file":
        assert output.read_text(encoding="utf-8") == "user file"
    elif existing_kind == "dangling":
        assert not output.exists()


def test_package_publication_does_not_replace_racing_empty_directory(
        tmp_path, monkeypatch):
    inputs = _make_package_inputs(tmp_path)
    output = inputs[3]
    original_publish = package_release_module._publish_no_replace
    collision = {}

    def publish_after_collision(stage, destination):
        destination.mkdir()
        collision["inode"] = destination.stat().st_ino
        return original_publish(stage, destination)

    monkeypatch.setattr(
        package_release_module, "_publish_no_replace", publish_after_collision
    )

    with pytest.raises(FileExistsError):
        _run_package(inputs)

    assert output.is_dir()
    assert output.stat().st_ino == collision["inode"]
    assert list(output.iterdir()) == []


def test_package_writes_exact_runtime_manifest_hashes_and_immutable_sums(tmp_path):
    inputs = _make_package_inputs(tmp_path)

    output = _run_package(inputs)

    manifest = json.loads((output / "release.json").read_text(encoding="utf-8"))
    assert manifest == {
        "schemaVersion": 1,
        "releaseId": "1.0.0-20260907-3aa9a27",
        "sourceCommit": "3aa9a2721d493584a40640d9a0147d802d9723e6",
        "platform": "ubuntu22.04-x86_64",
        "binaries": {
            "server": {
                "path": "bin/ev_admin_server",
                "sha256": hashlib.sha256((output / "bin/ev_admin_server").read_bytes()).hexdigest(),
            },
            "simulator": {
                "path": "bin/ev_charger_simulator",
                "sha256": hashlib.sha256(
                    (output / "bin/ev_charger_simulator").read_bytes()
                ).hexdigest(),
            },
            "client": {
                "path": "bin/ev_user_client",
                "sha256": hashlib.sha256((output / "bin/ev_user_client").read_bytes()).hexdigest(),
            },
        },
    }
    sum_lines = (output / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    summed_paths = [line.split("  ", 1)[1] for line in sum_lines]
    assert "config.local.ini" not in summed_paths
    assert "SHA256SUMS" not in summed_paths
    assert "release.json" in summed_paths
    for line in sum_lines:
        digest, relative = line.split("  ", 1)
        assert ".." not in Path(relative).parts
        assert not Path(relative).is_absolute()
        assert digest == hashlib.sha256((output / relative).read_bytes()).hexdigest()
    metadata = (output / "release.json").read_text() + (
        output / "licenses/dependencies.json"
    ).read_text() + (output / "SHA256SUMS").read_text()
    assert "fake-test-map-key" not in metadata


def test_package_uses_explicit_support_database_and_resource_whitelists(tmp_path):
    inputs = _make_package_inputs(tmp_path)

    output = _run_package(inputs)

    assert (output / "support/portable_launcher.py").is_file()
    assert (output / "database/create_runtime_copy.py").is_file()
    assert (output / "database/build_golden.py").is_file()
    assert (output / "database/seed_demo.py").is_file()
    assert (output / "database/schema.sql").is_file()
    assert (output / "使用说明.md").read_text(encoding="utf-8") == (
        "fixture:docs/release/portable-release.md\n"
    )
    assert (output / "runtime/golden/core.db").is_file()
    assert (output / "libexec/QtWebEngineProcess").is_file()
    assert (output / "resources/qtwebengine_resources.pak").is_file()
    assert (output / "translations/qtwebengine_locales/zh-CN.pak").is_file()
    assert (output / "python/lib/python3.10/lib-dynload/_sqlite3.cpython-310-x86_64-linux-gnu.so").is_file()
    assert (output / "plugins/platforms/libqxcb.so").is_file()
    assert (output / "fonts/NotoSansCJK-Regular.ttc").is_file()
    assert (output / "licenses/fixture-runtime/copyright").is_file()
    assert (output / "licenses/common-licenses/GPL-3").read_text(
        encoding="utf-8"
    ) == "Fixture GPL-3 full text\n"
    assert not (output / ".git").exists()
    assert not (output / "private-notes.txt").exists()
    assert not (output / "python/lib/python3.10/__pycache__").exists()
    assert "__pycache__" not in (
        output / "licenses/dependencies.json"
    ).read_text(encoding="utf-8")


def test_each_qt_executable_directory_resolves_private_bundle_paths(tmp_path):
    output = _run_package(_make_package_inputs(tmp_path))

    for executable_dir in (output / "bin", output / "libexec"):
        config_path = executable_dir / "qt.conf"
        config = configparser.ConfigParser(interpolation=None)
        with config_path.open(encoding="utf-8") as stream:
            config.read_file(stream)
        prefix = (executable_dir / config["Paths"]["Prefix"]).resolve()
        resolved = {
            name: (prefix / config["Paths"][name]).resolve()
            for name in (
                "Libraries",
                "Plugins",
                "LibraryExecutables",
                "Data",
                "Translations",
            )
        }
        assert resolved == {
            "Libraries": output / "lib",
            "Plugins": output / "plugins",
            "LibraryExecutables": output / "libexec",
            "Data": output,
            "Translations": output / "translations",
        }


def test_common_license_texts_have_provenance_and_notice_path_mapping(tmp_path):
    output = _run_package(_make_package_inputs(tmp_path))

    provenance = json.loads(
        (output / "licenses/dependencies.json").read_text(encoding="utf-8")
    )
    all_sources = {
        source
        for package in provenance["packages"]
        for source in package["sourceFiles"]
    }
    assert "/usr/share/common-licenses/GPL-3" in all_sources
    assert "/usr/share/common-licenses/GPL" in all_sources
    notices = (output / "licenses/THIRD_PARTY_NOTICES.md").read_text(
        encoding="utf-8"
    )
    assert "/usr/share/common-licenses" in notices
    assert "licenses/common-licenses" in notices
    assert "must not read or ship" not in (output / "config.local.ini").read_text()


def test_package_never_writes_beside_authorized_config(tmp_path):
    inputs = _make_package_inputs(tmp_path)
    probe = inputs[4].with_name(inputs[4].name + ".sanitized-probe")
    probe.write_text("user-owned sibling", encoding="utf-8")

    _run_package(inputs)

    assert probe.read_text(encoding="utf-8") == "user-owned sibling"


def test_package_rejects_input_symlink_that_escapes_its_declared_root(tmp_path):
    inputs = _make_package_inputs(tmp_path)
    outside = tmp_path / "host-only-secret"
    outside.write_text("must never be collected", encoding="utf-8")
    stdlib_file = inputs[0] / "usr/lib/python3.10/os.py"
    stdlib_file.unlink()
    stdlib_file.symlink_to("../../../../host-only-secret")

    with pytest.raises(ValueError, match="escapes"):
        _run_package(inputs)
    assert not inputs[3].exists()


def test_package_rebases_valid_absolute_sysroot_symlink_and_records_target(
        tmp_path):
    inputs = _make_package_inputs(tmp_path)
    sysroot = inputs[0]
    target = sysroot / "etc/python3.10/sitecustomize.py"
    target.parent.mkdir(parents=True)
    target.write_text("# sysroot site customization\n", encoding="utf-8")
    link = sysroot / "usr/lib/python3.10/sitecustomize.py"
    link.symlink_to("/etc/python3.10/sitecustomize.py")
    listing = sysroot / "var/lib/dpkg/info/fixture-runtime.list"
    with listing.open("a", encoding="utf-8") as stream:
        stream.write("/usr/lib/python3.10/sitecustomize.py\n")
        stream.write("/etc/python3.10/sitecustomize.py\n")

    output = _run_package(inputs)

    bundled = output / "python/lib/python3.10/sitecustomize.py"
    assert not bundled.is_symlink()
    assert bundled.read_text(encoding="utf-8") == "# sysroot site customization\n"
    provenance = json.loads(
        (output / "licenses/dependencies.json").read_text(encoding="utf-8")
    )
    source_files = provenance["packages"][0]["sourceFiles"]
    assert "/etc/python3.10/sitecustomize.py" in source_files


def test_package_rejects_cyclic_sysroot_symlink(tmp_path):
    inputs = _make_package_inputs(tmp_path)
    stdlib = inputs[0] / "usr/lib/python3.10"
    first = stdlib / "cycle-first.py"
    second = stdlib / "cycle-second.py"
    first.symlink_to(second.name)
    second.symlink_to(first.name)

    with pytest.raises(ValueError, match="cyclic"):
        _run_package(inputs)
    assert not inputs[3].exists()


def test_provenance_handles_jammy_merged_usr_library_paths(tmp_path):
    inputs = _make_package_inputs(tmp_path)
    sysroot = inputs[0]
    (sysroot / "lib").symlink_to("usr/lib")
    listing = sysroot / "var/lib/dpkg/info/fixture-runtime.list"
    listing.write_text(
        listing.read_text(encoding="utf-8").replace(
            "/usr/lib/x86_64-linux-gnu/libssl.so.3",
            "/lib/x86_64-linux-gnu/libssl.so.3",
        ).replace(
            "/usr/lib/x86_64-linux-gnu/libcrypto.so.3",
            "/lib/x86_64-linux-gnu/libcrypto.so.3",
        ),
        encoding="utf-8",
    )

    output = _run_package(inputs)

    provenance = json.loads(
        (output / "licenses/dependencies.json").read_text(encoding="utf-8")
    )
    assert provenance["packages"][0]["package"] == "fixture-runtime"


@pytest.mark.parametrize(
    "missing",
    [
        "usr/lib/qt6/libexec/QtWebEngineProcess",
        "usr/share/qt6/resources/qtwebengine_resources.pak",
        "usr/share/qt6/translations/qtwebengine_locales/zh-CN.pak",
        "apps/user-client/ev_user_client",
        "runtime/golden/core.db",
    ],
)
def test_package_fails_without_required_program_or_runtime_resource(tmp_path, missing):
    inputs = list(_make_package_inputs(tmp_path))
    root = inputs[1] if missing.startswith(("apps/", "simulator/")) else (
        inputs[2] if missing.startswith("runtime/") else inputs[0]
    )
    (root / missing).unlink()

    with pytest.raises(FileNotFoundError, match="required"):
        _run_package(tuple(inputs))
    assert not inputs[3].exists()


def test_start_wrapper_keeps_space_arguments_and_private_environment_local(tmp_path):
    output = _run_package(_make_package_inputs(tmp_path))
    recorder = output / "python/bin/python3"
    recorder.write_text(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$PYTHONHOME\" \"$PYTHONPATH\" \"$PYTHONNOUSERSITE\" "
        "\"$FONTCONFIG_FILE\" \"$LD_LIBRARY_PATH\" \"$@\"\n",
        encoding="utf-8",
    )
    recorder.chmod(0o755)
    host_environment = os.environ.copy()
    host_environment["PYTHONHOME"] = "/host/python must not leak"
    host_environment["LD_LIBRARY_PATH"] = "/host/qt must not leak"

    result = subprocess.run(
        [str(output / "启动.sh"), "--run-id", "argument with spaces", "--software-rendering"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        env=host_environment,
    )

    lines = result.stdout.splitlines()
    assert lines[:5] == [
        str(output / "python"),
        str(output / "python/lib/python3.10") + ":" + str(output / "python/lib/python3.10/lib-dynload"),
        "1",
        str(output / "fonts/fonts.conf"),
        str(output / "lib"),
    ]
    assert lines[5:] == [
        str(output / "support/portable_launcher.py"),
        "start",
        "--run-id",
        "argument with spaces",
        "--software-rendering",
    ]
    assert os.environ.get("PYTHONHOME") != str(output / "python")


def test_start_wrapper_real_imports_do_not_write_bytecode_into_bundle(tmp_path):
    output = _run_package(_make_package_inputs(tmp_path))
    (output / "support/portable_runtime.py").write_text(
        "IMPORTED = True\n", encoding="utf-8"
    )
    (output / "database/build_golden.py").write_text(
        "IMPORTED = True\n", encoding="utf-8"
    )
    (output / "support/portable_launcher.py").write_text(
        "import sys\n"
        "from pathlib import Path\n"
        "bundle = Path(__file__).resolve().parents[1]\n"
        "sys.path.insert(0, str(bundle / 'database'))\n"
        "import portable_runtime\n"
        "import build_golden\n",
        encoding="utf-8",
    )
    interpreter = output / "python/bin/python3"
    interpreter.write_text(
        "#!/bin/sh\n"
        "unset PYTHONHOME PYTHONPATH\n"
        f"exec {shlex.quote(sys.executable)} \"$@\"\n",
        encoding="utf-8",
    )
    interpreter.chmod(0o755)
    host_environment = os.environ.copy()
    host_environment.pop("PYTHONDONTWRITEBYTECODE", None)

    subprocess.run(
        [str(output / "启动.sh")],
        check=True,
        env=host_environment,
        cwd=tmp_path,
    )

    assert not list((output / "support").rglob("*.pyc"))
    assert not list((output / "database").rglob("*.pyc"))
    assert not (output / "support/__pycache__").exists()
    assert not (output / "database/__pycache__").exists()


def test_fontconfig_wrapper_limits_movable_font_search_to_bundle_fonts(tmp_path):
    output = _run_package(_make_package_inputs(tmp_path))
    font_uuid = output / "fonts/.uuid"
    assert len(font_uuid.read_bytes()) == 36
    assert uuid.UUID(font_uuid.read_text(encoding="ascii")).version == 4
    uuid_digest = hashlib.sha256(font_uuid.read_bytes()).hexdigest()
    assert f"{uuid_digest}  fonts/.uuid" in (
        output / "SHA256SUMS"
    ).read_text(encoding="utf-8").splitlines()
    relocated = tmp_path / "relocated package with spaces"
    output.rename(relocated)
    uuid_before = (relocated / "fonts/.uuid").read_bytes()
    recorder = relocated / "python/bin/python3"
    recorder.write_text(
        "#!/bin/sh\n"
        "printf '%s\\n' \"$PWD\" \"$FONTCONFIG_FILE\"\n",
        encoding="utf-8",
    )
    recorder.chmod(0o755)
    caller = tmp_path / "unrelated caller directory"
    caller.mkdir()

    result = subprocess.run(
        [str(relocated / "查看状态.sh")],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        cwd=caller,
    )

    assert result.stdout.splitlines() == [
        str(relocated),
        str(relocated / "fonts/fonts.conf"),
    ]
    font_config = (relocated / "fonts/fonts.conf").read_text(encoding="utf-8")
    assert "<dir>fonts</dir>" in font_config
    assert "prefix=\"relative\"" not in font_config
    assert str(output) not in font_config
    assert (relocated / "fonts/.uuid").read_bytes() == uuid_before
