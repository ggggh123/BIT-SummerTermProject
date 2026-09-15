# 第二阶段 Ubuntu 环境：安装、修补与验收

依据桌面 `Part2/06-TL-Hadoop伪分布式安装部署指南.md`（2026-09-14 版）。**本组不用老师下发的 hadoop-3.2.1 + jdk-8u261**，改用官方 tarball。

## 版本基线（全组必须一致）

| 组件 | 版本 | 安装路径 |
|---|---|---|
| JDK | Eclipse Temurin **17** | `/usr/local/jdk-17` |
| Hadoop | **3.4.1** | `/usr/local/hadoop` |
| Spark | **3.5.7** | `/usr/local/spark` |
| Python | 22.04 用系统 3.10；25.04 自建 **3.11** | `~/venvs/part2`（软链 `~/venv-py311`） |
| HDFS RPC | `hdfs://<主机名>:8020` | 副本数 1 |

## 脚本一览（按执行顺序）

| 脚本 | 用途 | 是否需要 sudo |
|---|---|---|
| `10-install-newstack.sh` | 校验 tarball → 停旧环境 → 装三个组件 → 写 7 个 Hadoop 配置(含 Java 17 `--add-opens`)→ 写 Spark 配置 → 写环境变量 → 删旧环境 → 格式化并启动 → 建 HDFS 目录并预传 jars | 是 |
| `11-fix-profile-and-start.sh` | 只补做"清 `/etc/profile` 旧块 + 格式化 + 启动 + 建目录" | 是 |
| `env-check.sh` | 只读体检：版本、配置、五进程、HDFS、Python 依赖、SSH 免密、共享文件夹 | 否 |
| `40-verify.sh` | 验收：版本、五进程、HDFS 读写、**Spark on YARN 读 HDFS**、SparkSQL、YARN 记录、Web UI | 否 |

```bash
# 前置：三个 tarball 下载到 ~/software/
#   jdk17.tar.gz                 （Adoptium Temurin 17，清华镜像）
#   hadoop-3.4.1.tar.gz          （清华 apache 镜像）
#   spark-3.5.7-bin-hadoop3.tgz  （清华没有，用华为云 mirrors.huaweicloud.com/apache/spark/）

sudo bash scripts/part2/10-install-newstack.sh   # 安装（在共享文件夹路径下执行）
bash scripts/part2/env-check.sh | tee env-report.txt   # 体检
bash scripts/part2/40-verify.sh                  # 验收
```

## 本机已记录的偏差（如实声明）

- 主机名保持 `TimeMachine`，操作用户 `spiderboy`（指南示例为「姓名全拼+数字」与 `hadoop`）
- Python 虚拟环境为 `~/venvs/part2`（并软链 `~/venv-py311` 对齐指南路径）
- YARN 容器内存 3072MB（指南建议 4096，本机总内存 5.3GB，指南允许下调）；`yarn-site`/`hdfs-site` 额外设 `bind-host=0.0.0.0`，便于宿主机浏览器直接打开 9870/8088
- 系统为 Ubuntu 25.04（指南双轨支持），apt 需切 `old-releases` 源

## 踩过的坑（务必转告其他成员）

| 现象 | 原因 | 处理 |
|---|---|---|
| `hadoop`/`hdfs` 报 `Cannot execute /opt/module/hadoop-3.2.1/libexec/...` | 旧环境把 `JAVA_HOME`/`HADOOP_HOME` 追加在 **`/etc/profile` 末尾**，而它在 `/etc/profile.d/` **之后**执行，覆盖了新路径 | `11-fix-profile-and-start.sh` 会删掉该块；`10-install-newstack.sh` 已内置自动清理 |
| `jps` 缺 NameNode | 未格式化，或上一版 Hadoop 的 `dfs/` 残留导致 clusterID 冲突 | 清空 `dfs/data` 后 `hdfs namenode -format` |
| YARN/NameNode 报 `InaccessibleObjectException` | Java 17 强封装，缺 `--add-opens` | 见指南 §5.7(1)(2)，`10-install-newstack.sh` 已写好 |
| `start-dfs.sh` 报 `can only be executed by root` | `hadoop-env.sh` 里 `HDFS_*_USER`/`YARN_*_USER` 被设成了 root | 改为当前用户（脚本已设） |
| Web UI 在宿主机打不开 | 默认只绑 `127.0.1.1`（hosts 把主机名解析到回环） | `bind-host=0.0.0.0`（脚本已配） |
| PySpark 报 Python 版本错误 | 25.04 系统 Python 是 3.13，PySpark 3.5 只支持 3.8–3.11 | `PYSPARK_PYTHON` 指向自建 3.11 环境 |

## 验收基线（2026-09-14 实测）

- `env-check.sh`：21 项全过
- `40-verify.sh`：12 项全过，其中 Spark on YARN 读 HDFS 成功，YARN 留下 `FINISHED/SUCCEEDED` 记录
- 宿主机浏览器可直接访问 `http://<VM-IP>:9870/`（HDFS）与 `:8088/`（YARN）
