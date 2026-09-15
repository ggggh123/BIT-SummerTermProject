# 第二阶段 Ubuntu 环境：安装、修补与验收

依据桌面 `Part2/06-TL-Hadoop伪分布式安装部署指南.md`（2026-09-14 版）。**本组不用老师下发的 hadoop-3.2.1 + jdk-8u261**，改用官方 tarball。

## 版本基线（全组必须一致）

| 组件 | 版本 | 安装路径 |
|---|---|---|
| JDK | Eclipse Temurin **17** | `/usr/local/jdk-17` |
| Hadoop | **3.4.1** | `/usr/local/hadoop` |
| Spark | **3.5.7** | `/usr/local/spark` |
| Node | **23.11.1**（老师要求 Node 23 及以上） | `/usr/local/node`，软链到 `/usr/local/bin/{node,npm,npx}` |
| Python | 22.04 用系统 3.10；25.04 自建 **3.11** | `~/venvs/part2`（软链 `~/venv-py311`） |
| HDFS RPC | `hdfs://<主机名>:8020` | 副本数 1 |

## 脚本一览（按执行顺序）

| 脚本 | 用途 | 是否需要 sudo |
|---|---|---|
| `10-install-newstack.sh` | 校验 tarball → 停旧环境 → 装三个组件 → 写 7 个 Hadoop 配置(含 Java 17 `--add-opens`)→ 写 Spark 配置 → 写环境变量 → 删旧环境 → 格式化并启动 → 建 HDFS 目录并预传 jars | 是 |
| `11-fix-profile-and-start.sh` | 只补做"清 `/etc/profile` 旧块 + 格式化 + 启动 + 建目录" | 是 |
| `env-check.sh` | 只读体检：版本、配置、五进程、HDFS、Python 依赖、SSH 免密、共享文件夹 | 否 |
| `40-verify.sh` | 验收：版本、五进程、HDFS 读写、**Spark on YARN 读 HDFS**、SparkSQL、YARN 记录、Web UI | 否 |
| **`30-start-demo.sh`** | **一键起全栈（跨机适配版）**：挑 Python → HDFS/YARN → ADS 库 → `web/dist` → Flask 同进程托管大屏与 `/api/*` | 否 |
| **`31-stop-demo.sh`** | 一键停（默认只停 Flask；`--all` 再按反向顺序停 YARN → HDFS） | 否 |

## 一键启停：跨机适配版（2026-09-15 合并）

`30-start-demo.sh` / `31-stop-demo.sh` 已合并原 `part2/scripts/start_part2.sh`（#2 集成机专用版）的跨机兼容能力，同一套脚本可适配组内两种部署形态：

| 差异点 | 形态 A：Hadoop 以当前登录用户运行 | 形态 B：Hadoop 以独立 `hadoop` 用户运行（集成机 `niyujun01`） |
|---|---|---|
| `jps` 枚举进程 | 直接可用 | 需 `sudo -u hadoop bash -lc "jps"`——**非 login shell 的 PATH 不含 JDK，`jps` 会报"找不到命令"** |
| Hadoop 启停 | 直接调 `sbin/start-*.sh` | 需 `sudo -u hadoop bash -lc <script>` |
| **成功判据** | **以"守护进程是否真的出现/消失"为准，不信任脚本退出码**——实测：以无权限用户运行 `start-dfs.sh` 会**返回 0 却不启动任何进程**，只看退出码会误报成功 | 同 |
| 路径与解释器 | 均可环境变量覆盖：`PART2_ROOT` `PART2_PORT` `PART2_PYTHON` `PART2_LOG_DIR` `ADS_DB` `HADOOP_HOME` `PART2_HADOOP_USER` | 同 |
| 健康检查 | `curl` → `wget` → Python `urllib` 三级回退 | 同 |

脚本自动探测当前属于哪种形态，无需手工切换。
统一路由入口的最终归属见仓库 `part2/scripts/start_part2.sh`（#3 于 2026-09-15 重写，支持 `PART2_SKIP_HADOOP` 纯演示模式）。

### 30-start-demo.sh 与 part2/scripts/start_part2.sh 的分工

两者做同一件事，但适用范围不同：

| | `part2/scripts/start_part2.sh`（#3 重写版，主线推荐） | 本目录 `30-start-demo.sh` |
|---|---|---|
| 仓库根 | 按脚本位置自动推导（`PART2_ROOT` 可覆盖） | 按脚本位置自动推导 |
| Hadoop | `sudo -u hadoop ...`，支持 `PART2_SKIP_HADOOP=1` 跳过 | 自动探测形态，直接调 `start-dfs.sh`，已在运行则跳过 |
| Python | 系统 `python3` + 依赖探测 | 自动挑「能 `import flask`」的解释器（本机是 `~/venvs/part2/bin/python`） |
| 健康检查 | `curl` | `curl` → `wget` → Python `urllib` 三级回退 |

跨机适配的完整缺陷记录与给 #2 的补丁建议见
`docs/test/evidence/part2-2026-09-15/vm-integration-run.md` §2/§4。

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
| **一键脚本报"已启动"但进程没起来** | 以无权限用户执行 `start-dfs.sh` 返回 0 却不启动 | 已改为按进程存在性判定（2026-09-15） |
| **`jps` 在脚本里枚举不到守护进程** | 非 login shell 的 PATH 不含 JDK | 用 `sudo -u hadoop bash -lc "jps"` |

## 验收基线（2026-09-14 实测）

- `env-check.sh`：21 项全过
- `40-verify.sh`：12 项全过，其中 Spark on YARN 读 HDFS 成功，YARN 留下 `FINISHED/SUCCEEDED` 记录
- 宿主机浏览器可直接访问 `http://<VM-IP>:9870/`（HDFS）与 `:8088/`（YARN）

## 集成机补充实测（2026-09-15，`niyujun01`）

- `30-start-demo.sh` / `31-stop-demo.sh` 完整启停验证：全停干净 → 一键启动 `守护进程 5/5（jps 模式: sudo）`
- 数据已落 HDFS：`/ev-charging/{ods 104.5M, dws 3.9M, ads 5.5M}`，ODS 保持 `dt=YYYY-MM-DD` 分区
- Spark on YARN 从 HDFS 读回验证通过（`ods_stations` 行数 = 8）
- Node 升级至 **23.11.1** 后前端 `npm ci`（65 包）+ `npm run build` 正常，无回归
