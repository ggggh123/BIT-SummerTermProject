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

## 一键启停：统一入口（2026-09-15）

**唯一入口是 `part2/scripts/start_part2.sh` 与 `part2/scripts/stop_part2.sh`。**

本目录原有的 `30-start-demo.sh` / `31-stop-demo.sh`（#1 可移植版 + #2 加的跨机兼容层）**已在本次统一中删除**：其全部能力均已由 `start_part2.sh`（#3 重写版）覆盖，保留两套会继续扩散重复。

| 原 30/31 的能力 | 现 `start_part2.sh` 的实现 |
|---|---|
| 自动挑「能 `import flask`」的解释器 | `pick_python()`：多候选探测 + `PART2_PYTHON` 显式覆盖（不临时安装依赖） |
| 跨用户管理 Hadoop | `start_hadoop()`：`ev-part2` → 当前用户 `start-dfs.sh` → `sudo -n -u <HADOOP_USER>` 三级回退 |
| 不信任启停脚本退出码 | 改以 **TCP 端口监听判定（8020 / 8032）** 取代 `jps` 枚举，跨用户更可靠 |
| 健康检查多级回退 | 以 Python `urllib` 轮询 `/api/health` 并比对 `dbPath`，防止误连旧服务 |
| 路径与解释器可覆盖 | `PART2_ROOT` `PART2_PORT` `PART2_HOST` `PART2_LOG_DIR` `ADS_DB` `PART2_PYTHON` `PART2_HADOOP_USER` `PART2_SKIP_HADOOP` |

```bash
# 纯演示（不启 Hadoop，只读已物化 ADS）
PART2_SKIP_HADOOP=1 bash part2/scripts/start_part2.sh
# 完整链路（含 HDFS/YARN）
bash part2/scripts/start_part2.sh
bash part2/scripts/stop_part2.sh
```

两个仍然有效的跨机坑（务必转告组内）：

- **非 login shell 的 PATH 不含 JDK**：脚本里调 `jps` 会报"找不到命令"，需 `sudo -u hadoop bash -lc "jps"`；
- **以无权限用户执行 `start-dfs.sh` 会返回 0 却不启动任何进程**：只看退出码会误报成功，必须以端口/进程实际状态为准。

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
| **一键脚本报"已启动"但进程没起来** | 以无权限用户执行 `start-dfs.sh` 返回 0 却不启动 | 已改为按端口/进程存在性判定（2026-09-15） |
| **`jps` 在脚本里枚举不到守护进程** | 非 login shell 的 PATH 不含 JDK | 用 `sudo -u hadoop bash -lc "jps"` |

## 验收基线（2026-09-14 实测）

- `env-check.sh`：21 项全过
- `40-verify.sh`：12 项全过，其中 Spark on YARN 读 HDFS 成功，YARN 留下 `FINISHED/SUCCEEDED` 记录
- 宿主机浏览器可直接访问 `http://<VM-IP>:9870/`（HDFS）与 `:8088/`（YARN）

## 集成机补充实测（2026-09-15，`niyujun01`）

- `30-start-demo.sh` / `31-stop-demo.sh` 完整启停验证：全停干净 → 一键启动 `守护进程 5/5（jps 模式: sudo）`
  （该脚本已随统一入口删除，能力并入 `part2/scripts/start_part2.sh`）
- 数据已落 HDFS：`/ev-charging/{ods 104.5M, dws 3.9M, ads 5.5M}`，ODS 保持 `dt=YYYY-MM-DD` 分区
- Spark on YARN 从 HDFS 读回验证通过（`ods_stations` 行数 = 8）
- Node 升级至 **23.11.1** 后前端 `npm ci`（65 包）+ `npm run build` 正常，无回归
- 集成机运行时入口 `/usr/local/ev-part2/{env.sh,bin/cluster.sh}` 为**适配版**：指向本组统一基线
  JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 / 系统 Python 3.10，并以 `HADOOP_USER_NAME=hadoop` 访问 HDFS。
  未采用 `install_global_runtime.sh` 自带的 JDK 8 + Hadoop 3.2.1 + 自建 CPython 组合，避免与本机既有环境互相覆盖。
## 跑数据链路（50–53）：没有全局运行时的机器怎么跑官方链路

官方入口 `part2/scripts/run_pipeline.sh` 依赖全局运行时 `/usr/local/ev-part2`（JDK 8 + Python 3.10，
由 `part2/scripts/install_global_runtime.sh` 配合 sudo 安装）。成员机上往往**拿不到 sudo 口令**，
装不了那一套，于是「官方链路在成员机上跑不起来」成了最常见的阻塞。

下面四个脚本用一层 shim 把官方脚本对全局运行时的调用映射到本机既有工具链
（JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 + 自建 venv），**不改动仓库里任何官方脚本**：

| 脚本 | 作用 | 是否需要 sudo |
|---|---|---|
| `50-install-shims.sh` | 装 `~/ev-shim/ev-part2` 与 `~/ev-shim/spark-submit-derby.sh`（把 Derby 元数据与 warehouse 落到本机磁盘，避免共享目录上的锁/权限问题） | 否 |
| `51-run-prl.sh` | 跑 PRL 质量检测 + 清洗（Spark on YARN），产出 `$HDFS_ROOT/quality/batches/<run-id>/`；**不碰**正式 `$HDFS_ROOT/dwd` | 否 |
| `52-publish-dwd.sh` | 把批次里的 `dwd/` 发布到 `$HDFS_ROOT/dwd` → `spark-sql -f dwd_contract.sql` 注册 Hive 外部表 → 4 张分区表 `MSCK REPAIR` → 打印各表行数 | 否 |
| `53-run-dws-ads.sh` | 调官方 `part2/scml/scripts/run_dws_ads.sh` 跑 DWS/ADS，并打印 `ads_meta` 关键值 | 否 |

完整顺序（在共享文件夹路径下执行，注意用 **`bash -l`**，否则 PATH 里没有 JDK/Hadoop/Spark）：

```bash
bash -l scripts/part2/50-install-shims.sh
bash -l scripts/part2/51-run-prl.sh --extra --accept-draft   # 策略未冻结时官方跑法需要 --accept-draft
bash -l scripts/part2/52-publish-dwd.sh                      # 默认用 51 记下的 last-run-id
bash -l scripts/part2/53-run-dws-ads.sh --skip-reconcile     # 见下面「对账假报警」
```

**为什么 52 不能省**：`run_dws_ads.sh` **不会**执行 `dwd_contract.sql`。只把 DWD 传上 HDFS 就跑
DWS/ADS，会直接报 `TABLE_OR_VIEW_NOT_FOUND ev_charging.dwd_station_hourly`；不 `MSCK REPAIR`
则 4 张分区事实表查出来是 0 行。

## 体检与取证（60–63）：数字从哪来，怎么当众重跑

| 脚本 | 作用 | 在哪跑 |
|---|---|---|
| `60-status.sh` | 一键体检：5 个 Hadoop 守护进程 / 演示服务 HTTP + `dbPath` + KPI / HDFS 各层大小与条目数 / `ads.db` 的 `ads_meta` 与预测行数。只读，退出码 0=全正常 | 虚拟机 |
| `61-dump-api.sh` | 抓全部 27 个契约端点的原始 JSON（无 curl 时自动回退 wget） | 虚拟机或宿主机 |
| `62-capture-pages.mjs` | 进真实页面取每个图表**实际渲染的 option**（`EChart.vue` 把实例挂在 `.chart` 容器的 `__echarts` 上），并可选截图；输出 `NN-<page>.json` 与 `NN-<page>.png` | 宿主机（要 `web/node_modules` 的 playwright + Chrome） |
| `63-ads-truth.py` | 从 `ads.db` 导出 ADS 真值（kpis/monthly/chargers/stations/districts/rfm/price/peak/quality/forecast 十段） | 任意有 python3 的机器 |

三个取证脚本正好对应 `part2-metric-checklist.md` 抽验表的**三段取值**：ADS 真值（63）、接口值（61）、
大屏显示值（62）。图里的数字用 `getOption()` 取而不是看截图读数：截图读数有误差也不可复现，
而 option 就是图表渲染的那组数，可以随时重跑。

```bash
bash -l scripts/part2/60-status.sh
bash -l scripts/part2/61-dump-api.sh
node scripts/part2/62-capture-pages.mjs --out docs/test/evidence/part2-2026-09-15/screenshots
python3 scripts/part2/63-ads-truth.py --out runtime/ads-truth.json
```

## 已知缺陷：`run_dws_ads.sh` 的内置对账会「假报警」

第 5 步对账在新布局下必然报错（本机实测 24/30、6 项失败），**不是数据问题**：

1. 小时表容差写死 0（15,073 vs 15,120，缺的 47 小时是 PRL 已声明的事实）；
2. 它的「重算」走 SCML 的 Python 清洗口径（112,422 单 / 287 桩），与 PRL 清洗后的官方口径（97,804 单 / 251 桩）不可比；
3. 找 DWD 时不认 `dt=` 分区目录。

看干净产出用 `53-run-dws-ads.sh --skip-reconcile`，把对账结论单独记录，别让它掩盖真实产出。
修复归属见 `docs/management/part2-defect-log.md`。
