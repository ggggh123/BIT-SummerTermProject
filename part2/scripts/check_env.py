"""只读自检：精确核对工具版本；--live 额外检查守护进程和 HDFS/YARN。"""
import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path
from urllib.request import build_opener, ProxyHandler


def run(command):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    if result.returncode:
        raise RuntimeError(result.stdout[-2000:])
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    checks = []
    def check(name, callback):
        try:
            detail = callback()
            checks.append({"name": name, "ok": True, "detail": detail})
        except Exception as exc:
            checks.append({"name": name, "ok": False, "detail": str(exc)})
    def version(command, expected):
        actual = run(command)
        if expected not in actual:
            raise RuntimeError(f"期望 {expected}，实际：{actual}")
        return expected
    check("架构", lambda: version(["uname", "-m"], "x86_64"))
    check("Python 3.10", lambda: version([sys.executable, "--version"], "Python 3.10.21"))
    check("Java 8", lambda: version(["java", "-version"], "1.8.0_504"))
    check("Hadoop", lambda: version(["hadoop", "version"], "Hadoop 3.2.1"))
    check("Spark", lambda: version(["spark-submit", "--version"], "version 3.5.7"))
    check("Python 可直接导入 PySpark", lambda: version([sys.executable, "-c", "import pyspark; print(pyspark.__version__)"], "3.5.7"))
    if args.live:
        def processes():
            names = {line.split()[-1] for line in run(["jps", "-l"]).splitlines()}
            expected = {"org.apache.hadoop.hdfs.server.namenode.NameNode", "org.apache.hadoop.hdfs.server.datanode.DataNode", "org.apache.hadoop.hdfs.server.namenode.SecondaryNameNode", "org.apache.hadoop.yarn.server.resourcemanager.ResourceManager", "org.apache.hadoop.yarn.server.nodemanager.NodeManager"}
            if not expected.issubset(names):
                raise RuntimeError(f"缺少守护进程：{sorted(expected - names)}")
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
    report = {"ok": all(item["ok"] for item in checks), "scope": "local-ubuntu25-development-only", "ubuntu22_verified": False, "os": platform.freedesktop_os_release().get("PRETTY_NAME"), "python": sys.executable, "checks": checks}
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as target:
            target.write(output)
    print(output, end="")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
