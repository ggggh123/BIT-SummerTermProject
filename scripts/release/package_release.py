"""Collect a self-contained Ubuntu 22.04 x86_64 portable release tree."""
from __future__ import annotations

import configparser
from collections import deque
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


LIBRARY_DIRS = (
    "lib/x86_64-linux-gnu",
    "usr/lib/x86_64-linux-gnu",
    "lib64",
    "lib",
    "usr/lib64",
    "usr/lib",
)
GLIBC_LIBRARIES = {
    "libanl.so.1",
    "libc.so.6",
    "libdl.so.2",
    "libm.so.6",
    "libmemusage.so",
    "libnsl.so.1",
    "libnss_compat.so.2",
    "libnss_dns.so.2",
    "libnss_files.so.2",
    "libnss_hesiod.so.2",
    "libpcprofile.so",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
    "libthread_db.so.1",
    "libutil.so.1",
}
PROGRAMS = {
    "server": ("apps/admin-server/ev_admin_server", "bin/ev_admin_server"),
    "simulator": ("simulator/ev_charger_simulator", "bin/ev_charger_simulator"),
    "client": ("apps/user-client/ev_user_client", "bin/ev_user_client"),
}
SUPPORT_FILES = {
    "scripts/release/portable_launcher.py": "support/portable_launcher.py",
    "scripts/release/portable_runtime.py": "support/portable_runtime.py",
    "scripts/demo_runtime.py": "support/demo_runtime.py",
    "scripts/demo_processes.py": "support/demo_processes.py",
    "scripts/demo_protocol.py": "support/demo_protocol.py",
    "docs/release/portable-release.md": "使用说明.md",
}
DATABASE_FILES = (
    "create_runtime_copy.py",
    "build_golden.py",
    "seed_demo.py",
    "schema.sql",
)
GOLDEN_FILES = ("core.db", "core.db.sha256", "core.manifest.json")
QT_PLUGIN_GROUPS = (
    "platforms",
    "sqldrivers",
    "imageformats",
    "iconengines",
    "tls",
    "platforminputcontexts",
    "xcbglintegrations",
    "networkinformation",
)
REQUIRED_QT_PLUGINS = (
    "platforms/libqxcb.so",
    "sqldrivers/libqsqlite.so",
    "imageformats/libqjpeg.so",
    "tls/libqopensslbackend.so",
)
REQUIRED_WEBENGINE_RESOURCES = (
    "qtwebengine_resources.pak",
    "qtwebengine_resources_100p.pak",
    "qtwebengine_resources_200p.pak",
    "qtwebengine_devtools_resources.pak",
)


def prepare_output(path: Path) -> Path:
    """Create and return a new output directory, refusing any existing path."""
    output = Path(path).resolve()
    output.mkdir(parents=True, exist_ok=False)
    return output


def _authorized_map_key(source: Path) -> str:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    try:
        with Path(source).open(encoding="utf-8") as stream:
            config.read_file(stream)
    except (OSError, configparser.Error) as exc:
        raise ValueError("authorized INI config is missing or malformed") from exc
    key = config.get("tencent", "mapKey", fallback="").strip().strip('"').strip()
    if not key or "\n" in key or "\r" in key:
        raise ValueError("authorized INI config needs a non-empty tencent/mapKey")
    return key


def write_sanitized_config(source: Path, destination: Path) -> None:
    """Copy only the authorized map key into a fixed loopback configuration."""
    key = _authorized_map_key(source)
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(
        "[server]\n"
        "host = 127.0.0.1\n"
        "port = 9100\n\n"
        "[tencent]\n"
        f"mapKey = {key}\n",
        encoding="utf-8",
    )
    target.chmod(0o600)


def read_needed(path: Path) -> list[str]:
    """Return DT_NEEDED names using the host readelf without executing the ELF."""
    try:
        result = subprocess.run(
            ["readelf", "--dynamic", str(Path(path))],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValueError(f"cannot read ELF dynamic section: {path}") from exc
    return re.findall(r"\(NEEDED\).*?Shared library: \[([^\]]+)\]", result.stdout)


def _is_system_boundary(name: str) -> bool:
    return (
        name in GLIBC_LIBRARIES
        or name.startswith("ld-linux-")
        or name.startswith("ld64.so")
    )


def _within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _resolve_symlink_chain(sysroot: Path, source: Path) -> tuple[Path, list[Path]]:
    """Resolve a sysroot symlink without allowing an absolute host escape."""
    root = Path(sysroot).resolve()
    current = Path(source).absolute()
    chain: list[Path] = []
    seen: set[Path] = set()
    while current.is_symlink():
        normalized = Path(os.path.normpath(current))
        if not _within(normalized, root) or normalized in seen:
            raise ValueError(f"unsafe or cyclic sysroot symlink: {source}")
        seen.add(normalized)
        chain.append(normalized)
        link = Path(os.readlink(normalized))
        current = root / str(link).lstrip("/") if link.is_absolute() else normalized.parent / link
        current = Path(os.path.normpath(current))
    current = current.resolve()
    if not _within(current, root) or not current.is_file():
        raise ValueError(f"sysroot symlink escapes or has no file target: {source}")
    return current, chain


def resolve_sysroot_library(sysroot: Path, name: str) -> Path:
    if Path(name).name != name or "/" in name:
        raise ValueError(f"invalid DT_NEEDED library name: {name}")
    root = Path(sysroot).resolve()
    for relative in LIBRARY_DIRS:
        candidate = root / relative / name
        if os.path.lexists(candidate):
            real, _ = _resolve_symlink_chain(root, candidate)
            return real
    raise FileNotFoundError(f"sysroot does not provide required library: {name}")


def _copy_sysroot_library(sysroot: Path, requested_name: str, destination: Path) -> Path:
    root = Path(sysroot).resolve()
    source_link = None
    for relative in LIBRARY_DIRS:
        candidate = root / relative / requested_name
        if os.path.lexists(candidate):
            source_link = candidate
            break
    if source_link is None:
        raise FileNotFoundError(f"sysroot does not provide required library: {requested_name}")
    real, chain = _resolve_symlink_chain(root, source_link)
    target_dir = Path(destination)
    target_dir.mkdir(parents=True, exist_ok=True)
    real_target = target_dir / real.name
    if os.path.lexists(real_target):
        if real_target.is_symlink() or real_target.read_bytes() != real.read_bytes():
            raise ValueError(f"conflicting bundled library name: {real.name}")
    else:
        shutil.copy2(real, real_target)
    for alias in [item.name for item in chain] + [requested_name]:
        alias_target = target_dir / alias
        if alias == real.name:
            continue
        if os.path.lexists(alias_target):
            if not alias_target.is_symlink() or os.readlink(alias_target) != real.name:
                raise ValueError(f"conflicting bundled library alias: {alias}")
        else:
            alias_target.symlink_to(real.name)
    return real


def collect_elf_dependencies(
        sysroot: Path, seeds: list[Path], destination: Path) -> set[Path]:
    """Recursively copy non-glibc DT_NEEDED libraries resolved only in sysroot."""
    pending = deque(Path(path) for path in seeds)
    inspected: set[Path] = set()
    collected: set[Path] = set()
    copied_names: set[str] = set()
    while pending:
        elf = pending.popleft()
        identity = elf.resolve()
        if identity in inspected:
            continue
        inspected.add(identity)
        for name in read_needed(elf):
            if _is_system_boundary(name) or name in copied_names:
                continue
            copied_names.add(name)
            real = _copy_sysroot_library(sysroot, name, destination)
            if real not in collected:
                collected.add(real)
                pending.append(real)
    return collected


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _required_file(path: Path, allowed_root: Path | None = None) -> Path:
    candidate = Path(path)
    if not candidate.is_file():
        raise FileNotFoundError(f"required release input is missing: {candidate}")
    if allowed_root is not None and not _within(candidate.resolve(), Path(allowed_root).resolve()):
        raise ValueError(f"release input symlink escapes its declared root: {candidate}")
    return candidate


def _copy_file(
        source: Path, destination: Path, allowed_root: Path | None = None) -> Path:
    src = _required_file(source, allowed_root)
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, target)
    return src.resolve()


def _copy_tree(
        source: Path, destination: Path, allowed_root: Path | None = None) -> list[Path]:
    src = Path(source)
    if not src.is_dir():
        raise FileNotFoundError(f"required release directory is missing: {src}")
    allowed = Path(allowed_root).resolve() if allowed_root is not None else src.resolve()
    for path in src.rglob("*"):
        if (path.is_file() or path.is_symlink()) and not _within(path.resolve(), allowed):
            raise ValueError(f"release input symlink escapes its declared root: {path}")
    shutil.copytree(
        src,
        destination,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
    )
    return [
        path.resolve()
        for path in src.rglob("*")
        if path.is_file()
        and "__pycache__" not in path.relative_to(src).parts
        and path.suffix not in (".pyc", ".pyo")
    ]


def _copy_matching_files(
        source: Path, destination: Path, pattern: str,
        allowed_root: Path | None = None) -> list[Path]:
    src = Path(source)
    if not src.is_dir():
        raise FileNotFoundError(f"required release directory is missing: {src}")
    copied = []
    for path in sorted(src.glob(pattern)):
        if path.is_file():
            copied.append(_copy_file(
                path, Path(destination) / path.name, allowed_root
            ))
    if not copied:
        raise FileNotFoundError(f"required release files are missing: {src}/{pattern}")
    return copied


def _validate_inputs(
        sysroot: Path, build_dir: Path, source_dir: Path, config_file: Path) -> None:
    for name, directory in (
        ("sysroot", sysroot), ("build", build_dir), ("source", source_dir)
    ):
        if not Path(directory).is_dir():
            raise FileNotFoundError(f"required {name} directory is missing: {directory}")
    for build_relative, _ in PROGRAMS.values():
        _required_file(Path(build_dir) / build_relative, build_dir)
    for source_relative in (*SUPPORT_FILES,):
        _required_file(Path(source_dir) / source_relative, source_dir)
    for name in DATABASE_FILES:
        _required_file(Path(source_dir) / "database" / name, source_dir)
    for name in GOLDEN_FILES:
        _required_file(Path(source_dir) / "runtime/golden" / name, source_dir)
    plugin_root = Path(sysroot) / "usr/lib/x86_64-linux-gnu/qt6/plugins"
    for relative in REQUIRED_QT_PLUGINS:
        _required_file(plugin_root / relative, sysroot)
    _required_file(Path(sysroot) / "usr/lib/qt6/libexec/QtWebEngineProcess", sysroot)
    resource_root = Path(sysroot) / "usr/share/qt6/resources"
    for name in REQUIRED_WEBENGINE_RESOURCES:
        _required_file(resource_root / name, sysroot)
    locale_root = Path(sysroot) / "usr/share/qt6/translations/qtwebengine_locales"
    for name in ("en-US.pak", "zh-CN.pak"):
        _required_file(locale_root / name, sysroot)
    _required_file(Path(sysroot) / "usr/bin/python3.10", sysroot)
    if not (Path(sysroot) / "usr/lib/python3.10/lib-dynload").is_dir():
        raise FileNotFoundError("required Python lib-dynload directory is missing")
    _required_file(
        Path(sysroot) / "usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        sysroot,
    )
    for name in ("libssl.so.3", "libcrypto.so.3"):
        resolve_sysroot_library(sysroot, name)
    # Parse the authorized input now, before creating a release tree.  This never logs the key.
    _authorized_map_key(config_file)


def _write_wrappers(bundle: Path) -> None:
    common = r'''#!/bin/sh
set -eu
bundle=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
export PYTHONHOME="$bundle/python"
export PYTHONPATH="$bundle/python/lib/python3.10:$bundle/python/lib/python3.10/lib-dynload"
export PYTHONNOUSERSITE=1
export FONTCONFIG_FILE="$bundle/fonts/fonts.conf"
export FONTCONFIG_PATH="$bundle/fonts"
export LD_LIBRARY_PATH="$bundle/lib"
exec "$bundle/python/bin/python3" "$bundle/support/portable_launcher.py" __COMMAND__ "$@"
'''
    for filename, command in (
        ("启动.sh", "start"), ("停止.sh", "stop"), ("查看状态.sh", "status")
    ):
        path = bundle / filename
        path.write_text(common.replace("__COMMAND__", command), encoding="utf-8")
        path.chmod(0o755)
    (bundle / "bin/qt.conf").write_text(
        "[Paths]\n"
        "Prefix = ..\n"
        "Libraries = lib\n"
        "Plugins = plugins\n"
        "LibraryExecutables = libexec\n"
        "Data = .\n"
        "Translations = translations\n",
        encoding="utf-8",
    )
    (bundle / "fonts/fonts.conf").write_text(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n"
        "  <dir prefix=\"relative\">.</dir>\n"
        "  <cachedir prefix=\"xdg\">fontconfig</cachedir>\n"
        "  <alias><family>sans-serif</family><prefer>"
        "<family>Noto Sans CJK SC</family></prefer></alias>\n"
        "</fontconfig>\n",
        encoding="utf-8",
    )


def _dpkg_status(sysroot: Path) -> dict[str, dict[str, str]]:
    status_path = Path(sysroot) / "var/lib/dpkg/status"
    try:
        text = status_path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise FileNotFoundError(f"required dpkg status is missing: {status_path}") from exc
    records = {}
    for paragraph in text.split("\n\n"):
        fields = {}
        for line in paragraph.splitlines():
            if ": " in line and not line.startswith((" ", "\t")):
                key, value = line.split(": ", 1)
                fields[key] = value
        package = fields.get("Package")
        version = fields.get("Version")
        if package and version and fields.get("Status") == "install ok installed":
            records[package] = {"package": package, "version": version}
            architecture = fields.get("Architecture")
            if architecture:
                records[package + ":" + architecture] = records[package]
    return records


def _dpkg_owners(sysroot: Path) -> tuple[dict[str, str], dict[str, dict[str, str]]]:
    root = Path(sysroot)
    statuses = _dpkg_status(root)
    owners = {}
    info = root / "var/lib/dpkg/info"
    for listing in sorted(info.glob("*.list")):
        package = listing.name[:-5]
        for line in listing.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("/"):
                owners.setdefault(line, package)
                installed = root / line.lstrip("/")
                if os.path.lexists(installed):
                    owners.setdefault("@resolved:" + str(installed.resolve()), package)
    return owners, statuses


def _write_provenance(bundle: Path, sysroot: Path, sources: set[Path]) -> None:
    root = Path(sysroot).resolve()
    owners, statuses = _dpkg_owners(root)
    packages: dict[str, dict[str, object]] = {}
    for source in sorted({path.resolve() for path in sources}):
        if not _within(source, root):
            continue
        source_name = "/" + source.relative_to(root).as_posix()
        owner = owners.get(source_name) or owners.get("@resolved:" + str(source))
        record = statuses.get(owner or "")
        if record is None:
            raise ValueError(f"no installed-package provenance for sysroot file: {source_name}")
        package_name = str(record["package"])
        entry = packages.setdefault(package_name, {
            "package": package_name,
            "version": record["version"],
            "sourceFiles": [],
        })
        entry["sourceFiles"].append(source_name)
    licenses = bundle / "licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    for package_name, entry in packages.items():
        source = root / "usr/share/doc" / package_name / "copyright"
        if not source.is_file():
            raise FileNotFoundError(
                f"required redistribution copyright is missing for {package_name}: {source}"
            )
        destination = licenses / package_name / "copyright"
        _copy_file(source, destination, root)
        entry["sourceFiles"].sort()
    package_list = [packages[name] for name in sorted(packages)]
    (licenses / "dependencies.json").write_text(
        json.dumps({"schemaVersion": 1, "packages": package_list}, indent=2) + "\n",
        encoding="utf-8",
    )
    (licenses / "THIRD_PARTY_NOTICES.md").write_text(
        "# 第三方依赖来源与版权\n\n"
        "本目录按 Ubuntu Jammy 已安装包记录实际收集文件、版本和版权文本。\n\n"
        + "\n".join(
            f"- `{item['package']}` `{item['version']}`："
            f"`{item['package']}/copyright`"
            for item in package_list
        )
        + "\n",
        encoding="utf-8",
    )


def _assert_safe_bundle_tree(bundle: Path) -> None:
    root = Path(bundle).resolve()
    for path in bundle.rglob("*"):
        if path.is_symlink():
            target = path.resolve()
            if not _within(target, root):
                raise ValueError(f"bundled symlink escapes release root: {path}")
        relative = path.relative_to(bundle)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"unsafe bundled relative path: {relative}")


def _write_sums(bundle: Path) -> None:
    lines = []
    for path in sorted(bundle.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        relative = path.relative_to(bundle).as_posix()
        if relative == "config.local.ini":
            continue
        lines.append(f"{_sha256(path)}  {relative}")
    (bundle / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _populate_release(
        bundle: Path,
        sysroot: Path,
        build_dir: Path,
        source_dir: Path,
        config_file: Path,
        release_id: str,
        source_commit: str) -> None:
    sysroot_sources: set[Path] = set()
    binary_manifest = {}
    elf_seeds = []
    for role, (build_relative, bundle_relative) in PROGRAMS.items():
        destination = bundle / bundle_relative
        _copy_file(build_dir / build_relative, destination, build_dir)
        destination.chmod(destination.stat().st_mode | 0o111)
        elf_seeds.append(destination)
        binary_manifest[role] = {
            "path": bundle_relative,
            "sha256": _sha256(destination),
        }
    for source_relative, bundle_relative in SUPPORT_FILES.items():
        _copy_file(source_dir / source_relative, bundle / bundle_relative, source_dir)
    for name in DATABASE_FILES:
        _copy_file(
            source_dir / "database" / name, bundle / "database" / name, source_dir
        )
    for name in GOLDEN_FILES:
        _copy_file(
            source_dir / "runtime/golden" / name,
            bundle / "runtime/golden" / name,
            source_dir,
        )

    plugin_root = sysroot / "usr/lib/x86_64-linux-gnu/qt6/plugins"
    for group in QT_PLUGIN_GROUPS:
        source_group = plugin_root / group
        if not source_group.is_dir():
            continue
        for plugin in sorted(source_group.glob("*.so")):
            copied_source = _copy_file(
                plugin, bundle / "plugins" / group / plugin.name, sysroot
            )
            sysroot_sources.add(copied_source)
            elf_seeds.append(plugin)

    process = sysroot / "usr/lib/qt6/libexec/QtWebEngineProcess"
    sysroot_sources.add(_copy_file(
        process, bundle / "libexec/QtWebEngineProcess", sysroot
    ))
    (bundle / "libexec/QtWebEngineProcess").chmod(0o755)
    elf_seeds.append(process)
    sysroot_sources.update(_copy_matching_files(
        sysroot / "usr/share/qt6/resources", bundle / "resources", "*", sysroot
    ))
    sysroot_sources.update(_copy_matching_files(
        sysroot / "usr/share/qt6/translations/qtwebengine_locales",
        bundle / "translations/qtwebengine_locales", "*.pak", sysroot
    ))

    python = sysroot / "usr/bin/python3.10"
    sysroot_sources.add(_copy_file(python, bundle / "python/bin/python3", sysroot))
    (bundle / "python/bin/python3").chmod(0o755)
    elf_seeds.append(python)
    sysroot_sources.update(_copy_tree(
        sysroot / "usr/lib/python3.10", bundle / "python/lib/python3.10", sysroot
    ))
    extensions = sorted((sysroot / "usr/lib/python3.10/lib-dynload").glob("*.so"))
    if not extensions:
        raise FileNotFoundError("required Python dynamic extensions are missing")
    elf_seeds.extend(extensions)

    font_root = sysroot / "usr/share/fonts/opentype/noto"
    fonts = sorted(font_root.glob("NotoSansCJK-*.ttc"))
    if not fonts:
        raise FileNotFoundError("required redistributable CJK font is missing")
    for font in fonts:
        sysroot_sources.add(_copy_file(font, bundle / "fonts" / font.name, sysroot))

    for ssl_name in ("libssl.so.3", "libcrypto.so.3"):
        source = _copy_sysroot_library(sysroot, ssl_name, bundle / "lib")
        sysroot_sources.add(source)
        elf_seeds.append(source)
    sysroot_sources.update(collect_elf_dependencies(
        sysroot, elf_seeds, bundle / "lib"
    ))

    write_sanitized_config(config_file, bundle / "config.local.ini")
    release = {
        "schemaVersion": 1,
        "releaseId": release_id,
        "sourceCommit": source_commit,
        "platform": "ubuntu22.04-x86_64",
        "binaries": binary_manifest,
    }
    (bundle / "release.json").write_text(
        json.dumps(release, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    _write_wrappers(bundle)
    _write_provenance(bundle, sysroot, sysroot_sources)
    _assert_safe_bundle_tree(bundle)
    _write_sums(bundle)


def package_release(
        *, sysroot: Path, build_dir: Path, source_dir: Path, output_dir: Path,
        config_file: Path, release_id: str, source_commit: str) -> Path:
    """Collect one new portable directory from explicit build/sysroot inputs."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", release_id, re.ASCII):
        raise ValueError("release-id must contain 1-64 safe ASCII characters")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source-commit must be a full lowercase 40-hex Git object id")
    root = Path(sysroot).resolve()
    build = Path(build_dir).resolve()
    source = Path(source_dir).resolve()
    config = Path(config_file).resolve()
    output = Path(output_dir).resolve()
    if os.path.lexists(output):
        raise FileExistsError(f"refusing to overwrite existing output: {output}")
    _validate_inputs(root, build, source, config)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".portable-release-", dir=output.parent))
    try:
        _populate_release(
            stage, root, build, source, config, release_id, source_commit
        )
        if os.path.lexists(output):
            raise FileExistsError(f"refusing to overwrite existing output: {output}")
        stage.rename(output)
    except BaseException:
        if stage.exists():
            shutil.rmtree(stage)
        raise
    return output


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--config-file", type=Path, required=True)
    parser.add_argument("--release-id", required=True)
    parser.add_argument("--source-commit", required=True)
    return parser


def main(argv=None) -> int:
    args = _parser().parse_args(argv)
    output = package_release(
        sysroot=args.sysroot,
        build_dir=args.build_dir,
        source_dir=args.source_dir,
        output_dir=args.output_dir,
        config_file=args.config_file,
        release_id=args.release_id,
        source_commit=args.source_commit,
    )
    print(f"portable release directory created: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
