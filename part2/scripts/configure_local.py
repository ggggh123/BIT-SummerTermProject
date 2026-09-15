#!/usr/bin/env python3
"""为当前用户初始化本机配置；已有且不同的配置默认拒绝覆盖。

工具链路径**全部从环境变量解析**（由 ev-part2-env.sh 提供），不再写死任何 JDK/Python 版本：
    JAVA_HOME          当前 JDK（基线为 17，需 --add-opens）
    HADOOP_HOME        当前 Hadoop（基线为 3.4.1）
    PYSPARK_PYTHON     driver/executor 解释器（22.04 用系统 3.10；25.04 需指向自建 3.10/3.11）
    EV_PART2_YARN_MEMORY_MB   YARN 容器内存上限（默认按物理内存的 60% 取整，上限 6144）
换基线重建配置时设置 EV_PART2_CONFIG_MIGRATE=1：旧文件会先备份为 <name>.bak-<时间戳> 再覆盖。
"""
import os
import shutil
import time
from pathlib import Path
import xml.etree.ElementTree as ET

# JDK 17 起模块强封装，Hadoop 需显式开放这些包，否则守护进程报 InaccessibleObjectException。
JDK_ADD_OPENS = " ".join(
    f"--add-opens {pkg}=ALL-UNNAMED"
    for pkg in (
        "java.base/java.lang",
        "java.base/java.lang.reflect",
        "java.base/java.io",
        "java.base/java.net",
        "java.base/java.nio",
        "java.base/java.util",
        "java.base/java.util.concurrent",
        "java.base/java.util.concurrent.atomic",
        "java.base/sun.nio.ch",
        "java.base/sun.security.action",
        "java.base/jdk.internal.ref",
        "java.security.jgss/sun.security.krb5",
    )
)


def xml_config(values):
    root = ET.Element("configuration")
    for name, value in values.items():
        entry = ET.SubElement(root, "property")
        ET.SubElement(entry, "name").text = name
        ET.SubElement(entry, "value").text = str(value)
    ET.indent(root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="unicode") + "\n"


def write_new_or_same(path, text, migrate=False):
    if path.exists():
        if path.read_text(encoding="utf-8") != text:
            if not migrate:
                raise RuntimeError(
                    f"已有配置与模板不同，保留原文件，请人工核对：{path}\n"
                    "如确认要按新基线重建，设置 EV_PART2_CONFIG_MIGRATE=1 后重试（旧文件会先备份）。"
                )
            backup = path.with_name(f"{path.name}.bak-{time.strftime('%Y%m%dT%H%M%S')}")
            shutil.copy2(path, backup)
        else:
            return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def default_yarn_memory_mb():
    """按物理内存的 60% 取整到 512 的倍数，区间 [1024, 6144]。"""
    explicit = os.environ.get("EV_PART2_YARN_MEMORY_MB")
    if explicit:
        return int(explicit)
    total_mb = 6144
    try:
        with open("/proc/meminfo", encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("MemTotal:"):
                    total_mb = int(line.split()[1]) // 1024
                    break
    except OSError:
        pass
    value = int(total_mb * 0.6 // 512 * 512)
    return max(1024, min(6144, value))


def main():
    java_home = os.environ.get("JAVA_HOME")
    if not java_home:
        raise RuntimeError("未设置 JAVA_HOME：请先 source ev-part2-env.sh（或显式导出 JAVA_HOME）。")
    hadoop_home = os.environ.get("HADOOP_HOME", "/usr/local/hadoop")
    pyspark_python = os.environ.get("PYSPARK_PYTHON") or os.environ.get("EV_PART2_PYTHON")
    if not pyspark_python:
        raise RuntimeError("未设置 PYSPARK_PYTHON / EV_PART2_PYTHON：请先 source ev-part2-env.sh。")
    migrate = os.environ.get("EV_PART2_CONFIG_MIGRATE") == "1"

    base = Path(os.environ.get("EV_PART2_HOME", str(Path.home() / "ev-part2"))).resolve()
    if base == Path.home() or base == Path("/") or str(base).startswith("/mnt/hgfs/"):
        raise RuntimeError("运行目录必须是 Linux 原生磁盘上的专用子目录，不能是根目录、家目录本身或 HGFS。")
    state = base / "runtime"
    folders = ["hdfs/name", "hdfs/data", "hdfs/secondary", "hadoop-tmp", "logs/hadoop", "pids", "spark-local", "yarn/local", "yarn/logs"]
    for item in folders:
        (state / item).mkdir(parents=True, exist_ok=True)
    yarn_memory_mb = default_yarn_memory_mb()
    configs = {
        "core-site.xml": {
            "fs.defaultFS": "hdfs://localhost:8020",
            "hadoop.tmp.dir": str(state / "hadoop-tmp"),
        },
        "hdfs-site.xml": {
            "dfs.replication": 1,
            "dfs.namenode.name.dir": (state / "hdfs/name").as_uri(),
            "dfs.datanode.data.dir": (state / "hdfs/data").as_uri(),
            "dfs.namenode.checkpoint.dir": (state / "hdfs/secondary").as_uri(),
            "dfs.namenode.rpc-address": "localhost:8020",
            "dfs.namenode.http-address": "127.0.0.1:9870",
            "dfs.namenode.secondary.http-address": "127.0.0.1:9868",
            "dfs.datanode.address": "127.0.0.1:9866",
            "dfs.datanode.ipc.address": "127.0.0.1:9867",
            "dfs.datanode.http.address": "127.0.0.1:9864",
        },
        "mapred-site.xml": {
            "mapreduce.framework.name": "yarn",
            "mapreduce.application.classpath": f"{hadoop_home}/share/hadoop/mapreduce/*:{hadoop_home}/share/hadoop/mapreduce/lib/*",
        },
        "yarn-site.xml": {
            "yarn.resourcemanager.hostname": "localhost",
            "yarn.resourcemanager.bind-host": "127.0.0.1",
            "yarn.resourcemanager.address": "localhost:8032",
            "yarn.resourcemanager.webapp.address": "127.0.0.1:8088",
            "yarn.nodemanager.hostname": "localhost",
            "yarn.nodemanager.bind-host": "127.0.0.1",
            "yarn.nodemanager.webapp.address": "127.0.0.1:8042",
            "yarn.nodemanager.aux-services": "mapreduce_shuffle",
            "yarn.nodemanager.resource.memory-mb": yarn_memory_mb,
            "yarn.nodemanager.resource.cpu-vcores": 4,
            "yarn.scheduler.minimum-allocation-mb": 512,
            "yarn.scheduler.maximum-allocation-mb": yarn_memory_mb,
            "yarn.scheduler.maximum-allocation-vcores": 4,
            "yarn.nodemanager.vmem-check-enabled": "false",
            "yarn.nodemanager.local-dirs": str(state / "yarn/local"),
            "yarn.nodemanager.log-dirs": str(state / "yarn/logs"),
            "yarn.nodemanager.env-whitelist": "JAVA_HOME,HADOOP_HOME,HADOOP_CONF_DIR,HADOOP_COMMON_HOME,HADOOP_HDFS_HOME,HADOOP_YARN_HOME,HADOOP_MAPRED_HOME,PATH,LANG,TZ,PYSPARK_PYTHON",
        },
    }
    # 先检查全部目标，避免前面的文件写完后才发现后面的冲突。
    texts = {base / "config/hadoop" / name: xml_config(values) for name, values in configs.items()}
    texts.update({
        base / "config/hadoop/hadoop-env.sh": (
            f"export JAVA_HOME={java_home}\n"
            f'export HADOOP_OPTS="$HADOOP_OPTS {JDK_ADD_OPENS}"\n'
            f'export HDFS_NAMENODE_OPTS="{JDK_ADD_OPENS}"\n'
            f'export HDFS_DATANODE_OPTS="{JDK_ADD_OPENS}"\n'
            f'export HDFS_SECONDARYNAMENODE_OPTS="{JDK_ADD_OPENS}"\n'
            "export HADOOP_HEAPSIZE_MAX=512\n"
        ),
        base / "config/hadoop/yarn-env.sh": (
            f"export JAVA_HOME={java_home}\n"
            f'export YARN_RESOURCEMANAGER_OPTS="{JDK_ADD_OPENS}"\n'
            f'export YARN_NODEMANAGER_OPTS="{JDK_ADD_OPENS}"\n'
            "export YARN_HEAPSIZE=512\n"
        ),
        base / "config/hadoop/workers": 'localhost\n',
        base / "config/spark/spark-env.sh": (
            f"export JAVA_HOME={java_home}\n"
            f"export PYSPARK_PYTHON={pyspark_python}\n"
            "export SPARK_DIST_CLASSPATH=$(hadoop classpath)\n"
        ),
        base / "config/spark/spark-defaults.conf": (
            "spark.master yarn\nspark.submit.deployMode client\nspark.driver.memory 1g\n"
            "spark.executor.memory 1g\nspark.executor.cores 2\nspark.executor.instances 1\n"
            "spark.sql.shuffle.partitions 8\nspark.sql.session.timeZone Asia/Shanghai\n"
            "spark.sql.ansi.enabled true\nspark.sql.legacy.timeParserPolicy CORRECTED\n"
            f"spark.pyspark.python {pyspark_python}\n"
            f"spark.yarn.appMasterEnv.PYSPARK_PYTHON {pyspark_python}\n"
            f"spark.executorEnv.PYSPARK_PYTHON {pyspark_python}\n"
        ),
    })
    # 外置配置目录同样需要发行版自带的日志与调度队列配置。
    for name in ("log4j.properties", "capacity-scheduler.xml", "hadoop-metrics2.properties"):
        stock = Path(hadoop_home) / "etc/hadoop" / name
        if stock.exists():
            texts[base / "config/hadoop" / name] = stock.read_text(encoding="utf-8")
    for path, text in texts.items():
        if path.exists() and path.read_text(encoding="utf-8") != text and not migrate:
            raise RuntimeError(
                f"已有配置不同，未覆盖：{path}\n"
                "如确认要按新基线重建，设置 EV_PART2_CONFIG_MIGRATE=1 后重试（旧文件会先备份）。"
            )
    for path, text in texts.items():
        write_new_or_same(path, text, migrate=migrate)
    print(f"本机配置已就绪：{base / 'config'}")
    print(f"  JDK          : {java_home}（已写入 JDK 17 所需 --add-opens）")
    print(f"  Hadoop       : {hadoop_home}")
    print(f"  PySpark 解释器: {pyspark_python}")
    print(f"  YARN 容器内存 : {yarn_memory_mb} MB")
    print("本机例外：当前账号、localhost、独立 $EV_PART2_HOME 配置；不修改系统主机名或账号。")


if __name__ == "__main__":
    main()
