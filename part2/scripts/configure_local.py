#!/usr/bin/env python3
"""为当前用户初始化本机配置；已有且不同的配置一律拒绝覆盖。"""
import os
from pathlib import Path
import xml.etree.ElementTree as ET


def xml_config(values):
    root = ET.Element("configuration")
    for name, value in values.items():
        entry = ET.SubElement(root, "property")
        ET.SubElement(entry, "name").text = name
        ET.SubElement(entry, "value").text = str(value)
    ET.indent(root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="unicode") + "\n"


def write_new_or_same(path, text):
    if path.exists():
        if path.read_text(encoding="utf-8") != text:
            raise RuntimeError(f"已有配置与模板不同，保留原文件，请人工核对：{path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main():
    base = Path(os.environ.get("EV_PART2_HOME", str(Path.home() / "ev-part2"))).resolve()
    if base == Path.home() or base == Path("/") or str(base).startswith("/mnt/hgfs/"):
        raise RuntimeError("运行目录必须是 Linux 原生磁盘上的专用子目录，不能是根目录、家目录本身或 HGFS。")
    state = base / "runtime"
    folders = ["hdfs/name", "hdfs/data", "hdfs/secondary", "hadoop-tmp", "logs/hadoop", "pids", "spark-local", "yarn/local", "yarn/logs"]
    for item in folders:
        (state / item).mkdir(parents=True, exist_ok=True)
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
            "mapreduce.application.classpath": "/usr/local/hadoop/share/hadoop/mapreduce/*:/usr/local/hadoop/share/hadoop/mapreduce/lib/*",
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
            "yarn.nodemanager.resource.memory-mb": 6144,
            "yarn.nodemanager.resource.cpu-vcores": 4,
            "yarn.scheduler.minimum-allocation-mb": 512,
            "yarn.scheduler.maximum-allocation-mb": 6144,
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
        base / "config/hadoop/hadoop-env.sh": 'export JAVA_HOME=/usr/local/ev-part2/jdk8\nexport HADOOP_HEAPSIZE_MAX=512\n',
        base / "config/hadoop/yarn-env.sh": 'export JAVA_HOME=/usr/local/ev-part2/jdk8\nexport YARN_HEAPSIZE=512\n',
        base / "config/hadoop/workers": 'localhost\n',
        base / "config/spark/spark-env.sh": 'export JAVA_HOME=/usr/local/ev-part2/jdk8\nexport PYSPARK_PYTHON=/usr/local/ev-part2/python/bin/python3.10\n',
        base / "config/spark/spark-defaults.conf": (
            "spark.master yarn\nspark.submit.deployMode client\nspark.driver.memory 1g\n"
            "spark.executor.memory 1g\nspark.executor.cores 2\nspark.executor.instances 1\n"
            "spark.sql.shuffle.partitions 8\nspark.sql.session.timeZone Asia/Shanghai\n"
            "spark.sql.ansi.enabled true\nspark.sql.legacy.timeParserPolicy CORRECTED\n"
            "spark.pyspark.python /usr/local/ev-part2/python/bin/python3.10\n"
            "spark.yarn.appMasterEnv.PYSPARK_PYTHON /usr/local/ev-part2/python/bin/python3.10\n"
            "spark.executorEnv.PYSPARK_PYTHON /usr/local/ev-part2/python/bin/python3.10\n"
        ),
    })
    # 外置配置目录同样需要发行版自带的日志与调度队列配置。
    for name in ("log4j.properties", "capacity-scheduler.xml", "hadoop-metrics2.properties"):
        stock = Path("/usr/local/hadoop/etc/hadoop") / name
        texts[base / "config/hadoop" / name] = stock.read_text(encoding="utf-8")
    for path, text in texts.items():
        if path.exists() and path.read_text(encoding="utf-8") != text:
            raise RuntimeError(f"已有配置不同，未覆盖：{path}")
    for path, text in texts.items():
        write_new_or_same(path, text)
    print(f"本机配置已就绪：{base / 'config'}")
    print("本机例外：Ubuntu 25.04、当前账号、localhost；不修改系统主机名或账号。")


if __name__ == "__main__":
    main()
