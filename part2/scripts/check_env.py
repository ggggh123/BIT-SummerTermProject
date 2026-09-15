"""只读自检：核对工具版本；--live 额外检查守护进程和 HDFS/YARN。

期望值默认取**团队冻结基线**（JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 / PySpark 3.5.7），
每一项都可用命令行参数或环境变量覆盖，便于旧基线机器或临时环境使用：

    --expect-java 1.8.0_504        EV_PART2_EXPECT_JAVA
    --expect-hadoop 3.2.1          EV_PART2_EXPECT_HADOOP
    --expect-spark 3.5.7           EV_PART2_EXPECT_SPARK
    --expect-python 3.10.21        EV_PART2_EXPECT_PYTHON   （默认只校验 3.8–3.11 区间）

报告中的 `scope` 与 `ubuntu22_verified` 依**实际操作系统**动态生成，不再写死。
"""
import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path
from urllib.request import build_opener, ProxyHandler

BASELINE_JAVA = "17"
BASELINE_HADOOP = "3.4.1"
BASELINE_SPARK = "3.5.7"
# PySpark 3.5 官方支持 3.8–3.11；默认只校验区间，不锁定补丁号
PYSPARK_PYTHON_MIN = (3, 8)
PYSPARK_PYTHON_MAX = (3, 11)


def run(command):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    if result.returncode:
        raise RuntimeError(result.stdout[-2000:])
    return result.stdout.strip()


def python_series(text):
    match = re.search(r"Python (\d+)\.(\d+)\.(\d+)", text)
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def resolve(cli_value, env_name, default):
    if cli_value:
        return cli_value
    return os.environ.get(env_name) or default


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-java")
    parser.add_argument("--expect-hadoop")
    parser.add_argument("--expect-spark")
    parser.add_argument("--expect-python")
    args = parser.parse_args()

    expect_java = resolve(args.expect_java, "EV_PART2_EXPECT_JAVA", BASELINE_JAVA)
    expect_hadoop = resolve(args.expect_hadoop, "EV_PART2_EXPECT_HADOOP", BASELINE_HADOOP)
    expect_spark = resolve(args.expect_spark, "EV_PART2_EXPECT_SPARK", BASELINE_SPARK)
    expect_python = resolve(args.expect_python, "EV_PART2_EXPECT_PYTHON", None)

    checks = []

    def check(name, callback):
        try:
            detail = callback()
            checks.append({"name": name, "ok": True, "detail": detail})
        except Exception as exc:
            checks.append({"name": name, "ok": False, "detail": str(exc)})

    def version(command, expected, label):
        actual = run(command)
        if expected not in actual:
            raise RuntimeError(f"期望 {expected}，实际：{actual}")
        return label or expected

    check("架构", lambda: version(["uname", "-m"], "x86_64", None))

    def check_python():
        actual = run([sys.executable, "--version"])
        series = python_series(actual)
        if series is None:
            raise RuntimeError(f"无法解析 Python 版本：{actual}")
        if expect_python:
            if expect_python not in actual:
                raise RuntimeError(f"期望 Python {expect_python}，实际：{actual}")
            return actual
        if not (PYSPARK_PYTHON_MIN <= series[:2] <= PYSPARK_PYTHON_MAX):
            raise RuntimeError(
                f"PySpark 3.5 支持 3.8–3.11，当前 {actual}；"
                "25.04 等系统请用 EV_PART2_PYTHON 指向自建 3.10/3.11 后重试"
            )
        return actual

    check("Python（3.8–3.11 或显式指定）", check_python)
    check("Java（基线 17）", lambda: version(["java", "-version"], expect_java, f"含 {expect_java}"))
    check("Hadoop（基线 3.4.1）", lambda: version(["hadoop", "version"], expect_hadoop, f"Hadoop {expect_hadoop}"))
    check("Spark", lambda: version(["spark-submit", "--version"], expect_spark, f"version {expect_spark}"))
    check(
        "Python 可直接导入 PySpark",
        lambda: version([sys.executable, "-c", "import pyspark; print(pyspark.__version__)"], expect_spark, expect_spark),
    )

    if args.live:
        # 守护进程可能由独立用户启动（如 hadoop）：以其它用户身份跑 jps 才能看到。
        hadoop_user = os.environ.get("EV_PART2_HADOOP_USER") or os.environ.get("PART2_HADOOP_USER") or ""
        current_user = run(["id", "-un"])

        def jps_command():
            if hadoop_user and hadoop_user != current_user:
                return ["sudo", "-n", "-u", hadoop_user, "bash", "-lc", "jps -l"]
            return ["jps", "-l"]

        def processes():
            names = {line.split()[-1] for line in run(jps_command()).splitlines()}
            expected = {"org.apache.hadoop.hdfs.server.namenode.NameNode", "org.apache.hadoop.hdfs.server.datanode.DataNode", "org.apache.hadoop.hdfs.server.namenode.SecondaryNameNode", "org.apache.hadoop.yarn.server.resourcemanager.ResourceManager", "org.apache.hadoop.yarn.server.nodemanager.NodeManager"}
            missing = sorted(expected - names)
            if missing:
                scope = f"（以 {hadoop_user} 身份枚举）" if hadoop_user and hadoop_user != current_user else ""
                raise RuntimeError(f"缺少守护进程{scope}：{missing}")
            return sorted(expected)

        check("五个守护进程", processes)
        check("HDFS 可读", lambda: run(["hdfs", "dfs", "-ls", "/ev-charging"]))

        def yarn():
            opener = build_opener(ProxyHandler({}))
            with opener.open("http://127.0.0.1:8088/ws/v1/cluster/info", timeout=5) as response:
                info = json.load(response)["clusterInfo"]
            if info["state"] != "STARTED":
                raise RuntimeError(f"YARN 状态异常：{info['state']}")
            return {key: info[key] for key in ("state", "hadoopVersion", "resourceManagerVersion")}

        check("YARN API", yarn)

    os_release = platform.freedesktop_os_release()
    version_id = (os_release.get("VERSION_ID") or "").strip()
    all_ok = all(item["ok"] for item in checks)
    if version_id.startswith("22.04"):
        scope = "integration-host-ubuntu2204"
    elif version_id.startswith("25.04"):
        scope = "dev-ubuntu2504"
    else:
        scope = f"local-ubuntu{version_id or 'unknown'}"
    report = {
        "ok": all_ok,
        "scope": scope,
        # 语义：本次自检确实运行在 Ubuntu 22.04 上且全部通过
        "ubuntu22_verified": bool(version_id.startswith("22.04") and all_ok),
        "os": os_release.get("PRETTY_NAME"),
        "python": sys.executable,
        "expected": {"java": expect_java, "hadoop": expect_hadoop, "spark": expect_spark, "python": expect_python or "3.8-3.11"},
        "checks": checks,
    }
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as target:
            target.write(output)
    print(output, end="")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
