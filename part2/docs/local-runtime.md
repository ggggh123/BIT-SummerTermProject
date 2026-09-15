# 本机第二阶段运行环境

本机继续使用 Ubuntu 25.04 x86_64。**不是 Ubuntu 22.04 验证结果，也不是全组环境已冻结。** 用户已确认继续使用当前虚拟机。

## 已选择的本机工具链

| 项目 | 本机版本／路径 |
|---|---|
| Java | Eclipse Temurin 8u504-b01，`/usr/local/ev-part2/jdk8` |
| Python | CPython 3.10.21，`/usr/local/ev-part2/python`，全局命令 `python3.10` |
| Hadoop | 3.2.1，`/usr/local/hadoop` → `/usr/local/hadoop-3.2.1` |
| Spark / 自带 PySpark | 3.5.7，`/usr/local/spark` → `/usr/local/spark-3.5.7-bin-hadoop3` |
| 全局入口 | `/usr/local/bin/ev-part2`；登录终端通过 `/etc/profile.d/ev-part2.sh` 加载工具路径 |
| 系统 Python | 保留 `/usr/bin/python3`，仍为系统 Python 3.13.3，不修改、不替换 |
| 本用户配置 | `/home/hushengyuan/ev-part2/config` |
| HDFS 元数据／块、YARN／Spark 工作目录 | `/home/hushengyuan/ev-part2/runtime`（Linux 原生磁盘，不在 HGFS） |
| 安装包与验证证据 | `/home/hushengyuan/ev-part2/downloads`、`/home/hushengyuan/ev-part2/evidence` |

以上均不是 Python venv／Conda 或临时安装目录。Python 3.10 显式提供给 Spark driver 和 worker，不通过改名系统 Python 实现。

Python 3.10 的全局 site-packages 中通过 `ev-part2-pyspark.pth` 指向 Spark 自带的 PySpark／Py4J，因此 `python3.10 -c 'import pyspark'` 可直接使用同一版本，不重复安装另一套 PySpark。守护进程停止宽限时间设为 20 秒，避免默认 5 秒过短导致 NodeManager 被提前强制终止。

## 与原设计的差异

- 保留当前 Ubuntu 25.04；Ubuntu 22.04 仍是团队交付基线，后续必须在真实 22.04 环境单独验证。
- 使用更新的 Java 8，而非老师包中的 8u261。Spark 3.5 官方已标记旧于 8u371 的 Java 8 支持弃用。本机选择不自动变更全组基线。
- 本机保持当前账号与主机名。各守护进程通过 Hadoop 自带 `--daemon` 直接启动，不修改 SSH 密钥、不创建 hadoop 用户、不改变主机名；课程是否要求姓名主机名／hadoop 账号，交由 #2 再核对。
- 这是五个守护进程的**单机伪分布式**，不是多节点集群。服务使用 localhost／回环接口；不依赖桥接或手机热点。
- HDFS RPC：localhost:8020；NameNode 页面：localhost:9870；YARN 页面：localhost:8088。

## 与远端集成分支的新差异

2026-09-15 检查 `origin/feat/part2-integration@eced236` 时，其 `part2/plans` 规定的是 JDK 17、Hadoop 3.4.1、Spark 3.5.7，并在 Ubuntu 25.04 使用 Python 3.11 venv。该配置来自队友分支，与本机用户先前批准的“全局环境、不用虚拟环境”不同，也没有被本文静默当成新基线。集成分支同日的真实跨机记录还确认 `part2/scripts/start_part2.sh` 写死 `/home/bit/part2`、`hadoop` 用户、系统 `python3` 和 `curl`，在另一台 VM 原样执行会中断；这属于待修复的启动脚本兼容性，不是 Hadoop 或 PRL 算法失败。

两套都使用 Spark 3.5.7 且业务代码不依赖 Java 8 私有 API，但这不等于已完成跨版本验收。最终集成前由 #2 选定唯一基线，再完整复跑 ODS→DWD→DWS→ADS。同一台机器不同时启动两套占用 8020/9870/8088/8042 的守护进程，也不用任一安装脚本覆盖另一套已有环境。

## 日常命令

```bash
ev-part2 configure
ev-part2 start
ev-part2 status
ev-part2 hdfs dfs -ls /ev-charging
ev-part2 python --version
ev-part2 spark-submit --version
ev-part2 stop
```

配置生成器遇到已存在且不同的文件会拒绝覆盖。启动脚本仅在专用 name/data 目录为空且尚未初始化时格式化一次；发现既有内容而元数据不完整时停止，不通过删数据或反复格式化“修复”。停止命令保留所有数据。

## 安装来源与校验

安装脚本先核对 SHA、完整读取归档，再安装；任何目标已存在则拒绝覆盖。不下载 Ubuntu 镜像、不改变系统软件源。

- Hadoop：老师已有 `hadoop-3.2.1.tar.gz`，SHA-256 `f66a3a4115b8f16c1077d1a198a06854dbef0e4233291712ed08d0a10629ed37`。此哈希记录的是本地教学安装包，不冒充官方签名验证。
- 老师原 JDK 包（保留原位、未作为活动运行时）：SHA-256 `5a04e01a091f6b1ed9c0b801be4fd10689af07eeb9e27f012c9aa3af9948ea34`。
- Java：[Adoptium 官方发行包](https://github.com/adoptium/temurin8-binaries/releases/tag/jdk8u504-b01)，SHA-256 `9c70e102f527ac674ac2fe9c7d47b9a04e2d19842ba5ab8e9b33f368bbadfaea`。
- Python：[Astral python-build-standalone 20260901](https://github.com/astral-sh/python-build-standalone/releases/tag/20260901) 中的 CPython 3.10.21 x86_64 GNU/Linux install-only，SHA-256 `73cc92db5e6fb07ba611dca3709956d57cc2ed74ebcc01515b550ea89dfe9cba`。
- Spark：[Apache Spark 3.5.7 官方归档](https://archive.apache.org/dist/spark/spark-3.5.7/)，SHA-512 固定在安装脚本中，与归档页提供值比对。
- 运行依据：[Spark 3.5.7 要求](https://spark.apache.org/docs/3.5.7/)、[Hadoop 3.2.1 单机部署](https://hadoop.apache.org/docs/r3.2.1/hadoop-project-dist/hadoop-common/SingleCluster.html)。
