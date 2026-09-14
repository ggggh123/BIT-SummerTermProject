# 02 · Hadoop 平台与 Flask 后端设计说明书（#2 TL）

> 承担人：#2（TL，技术负责人）
> 任务性质：**第二阶段主要任务（必做）**
> 管理职责：架构、接口、技术决策、系统集成
> 对应老师要求：（1）安装 Hadoop；（6）Flask 部分；并负责全链路集成

---

## 1. 模块目标

1. 在 Ubuntu 22.04 团队基线环境上完成 **Hadoop 分布式平台**（HDFS + YARN）与 **Spark** 的安装、配置与验证，为全组提供统一的大数据运行底座。
2. 开发 **Flask 后端服务**，读取 ADS 层结果，向前端大屏提供 REST API。
3. 作为 TL 负责各模块接口契约冻结与端到端集成。

## 2. Hadoop / Spark 环境设计

### 2.1 部署形态与环境基线

**部署形态**：5 台成员机各自安装单机伪分布式 Hadoop，互不组网；全链路集成与答辩演示固定在**一台集成/演示机**上完成。

| 项 | 方案 | 说明 |
|---|---|---|
| 模式 | **5 台成员机各自安装单机伪分布式 Hadoop（互不组网）**；全链路集成与答辩演示固定在**一台集成/演示机** | 伪分布式具备完整 NameNode/DataNode/YARN 体系；**如实说明：非多节点集群**（呼应《00》§1.4「不作虚假声明」） |
| OS | Ubuntu 22.04 LTS **或 25.04**（VMware 17） | 双轨支持；25.04 需切 `old-releases` 源并自建 Python 3.11（见《06》§3.3） |
| JDK | **Eclipse Temurin JDK 17**（官方 tarball） | LTS；Hadoop 3.4.1 与 Spark 3.5.7 共同支持；统一解压为 `/usr/local/jdk-17` |
| Hadoop | **3.4.1**（`hadoop-3.4.1.tar.gz`，单机伪分布式） | HDFS + YARN；3.4.0 起支持 Java 17；**全程使用官方 tarball，不使用老师安装包**，安装步骤见《06-TL-Hadoop伪分布式安装部署指南》 |
| Spark | **3.5.7**（`spark-3.5.7-bin-hadoop3.tgz`，on YARN） | PySpark + SparkSQL + MLlib；与 JDK 17 / Hadoop 3.4.1 兼容，**全组必须同版本**（选型依据见《06》§2.1） |
| Python | 22.04：系统 3.10；25.04：自建 3.11 虚拟环境 + pyspark | PySpark 用；**25.04 不要用系统 Python 3.13** |

**环境基线（全组必须一致）**：主机名规则「姓名全拼+两位数字」；HDFS RPC `hdfs://<主机名>:8020`（**不是 9000**）；NameNode UI `9870`、ResourceManager UI `8088`、ResourceManager RPC `8032`、副本数 `1`；HDFS 业务根 `/ev-charging/`。完整基线见《06-TL-Hadoop伪分布式安装部署指南》§2。

### 2.2 安装与配置要点

1. 创建 `hadoop` 用户并授予 sudo；修改主机名为「姓名全拼+数字」（如 `zhangsan01`）；配置 `/etc/hosts` 域名映射；配置 SSH 免密登录（`ssh-copy-id hadoop@<主机名>`）。
2. 安装 Temurin JDK 17 与 Hadoop 3.4.1 到 `/usr/local`（统一重命名为 `/usr/local/jdk-17`、`/usr/local/hadoop`），在 `~/.bashrc` 配置 `JAVA_HOME`、`HADOOP_HOME`、`YARN_HOME`、`PATH`。
3. 配置 7 个文件：`hadoop-env.sh` / `yarn-env.sh`（`JAVA_HOME` + **Java 17 必需的 `--add-opens`**）、`workers`（写入本机主机名）、`core-site.xml`（`fs.defaultFS=hdfs://<主机名>:8020`、`hadoop.tmp.dir`）、`hdfs-site.xml`（`dfs.replication=1`、name/data 目录）、`mapred-site.xml`（`mapreduce.framework.name=yarn` + 容器 `--add-opens`）、`yarn-site.xml`（`resourcemanager.address=<主机名>:8032` 等）。
4. `hdfs namenode -format` 后 `start-dfs.sh` / `start-yarn.sh`，`jps` 验证 NameNode、DataNode、ResourceManager、NodeManager、SecondaryNameNode 五个进程。
5. 验证：`hdfs dfs -mkdir /ev-charging` 并 `put` 读回样本文件；`http://localhost:9870`（HDFS Web UI）与 `http://localhost:8088`（YARN UI）可访问；用 `hadoop-mapreduce-examples` 跑一次 `pi` / `grep` 冒烟。
6. Spark 配置 `spark-env.sh`：`HADOOP_CONF_DIR` 指向 Hadoop 配置目录、`SPARK_DIST_CLASSPATH=$(hadoop classpath)`（直接用本机 Hadoop 客户端 jar）、`PYSPARK_PYTHON` 指向可用解释器（25.04 用自建 3.11）；`pyspark --master yarn` 跑通 `spark.read.text("hdfs://<主机名>:8020/...").count()`。
7. **环境一致性与自检**：每台部署后运行 `check_env.sh`，全绿即为合格。环境三件套（《环境基线》+《部署手册》+`check_env.sh`）见《06-TL-Hadoop伪分布式安装部署指南》，由 #4 纳入配置管理。

### 2.3 HDFS 目录规划

```text
/ev-charging/
├── ods/            # 原始生成数据（CSV/JSON，保留脏数据），按 dt=yyyy-MM-dd 分区
├── dwd/            # 清洗后明细（Parquet）
├── dws/            # 汇总层（Parquet）
├── ads/            # 应用指标层（Parquet + 导出 JSON）
├── quality/        # 质量探查与清洗报告
└── forecast/       # MLlib 模型与预测结果（选做）
```

### 2.4 集成/演示机与数据交接

**（1）集成/演示机**：全组指定一台 VM 为唯一权威的**集成/演示机**，负责端到端跑通 `生成器 → HDFS → 质量/清洗 → 数仓分层 → Flask → 前端大屏`。其余成员在各自 VM 上完成本模块开发与自测后推 git，由 TL 在集成机合并联调。拉伸目标（时间充裕再做）：把环境基线、部署手册与自检脚本复制到成员机，使**每台 VM 都能独立复现全链路**。

**（1.1）集成机是「角色」而非「机型」**：集成机与成员机共用同一套环境基线（JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 / `fs.defaultFS` 端口 `8020` / `dfs.replication=1` / 安装路径一致），差别仅为**应用层依赖 + 交接数据 + 集成职责**。因此任何一台 `check_env.sh` 全绿的成员机**无需重装 Hadoop**即可直接担任。

| 项 | 成员机（最小集） | 集成/演示机（超集） |
|---|---|---|
| 环境基线（JDK/Hadoop/Spark/端口/路径） | 必须一致 | 必须一致，**不重装** |
| HDFS 业务目录 `/ev-charging/{ods,dwd,dws,ads,quality,forecast}` | ✅ | ✅ |
| Python 依赖 | `pyspark` 为主 | + `flask`、`flask-cors`、`pandas`、`pyarrow` |
| Node 18+ 与前端 `npm run build` 产物 | 仅 #1 需要 | **必须**（由 Flask 静态托管） |
| `handoff/` 五个交接包 | 仅自己产物 | **必须齐备且 `manifest.json` 校验通过** |
| `start_part2.sh` / `stop_part2.sh` | 可选 | **必须**，答辩前连续两遍无人工干预 |
| 网络与端口 | HDFS/YARN UI 本机访问即可 | 演示需外部可达：桥接网卡 + 服务监听 `0.0.0.0` + 放行端口 |

**（1.2）从成员机升级为集成机的检查清单**：

1. `check_env.sh` 全绿（版本/路径/端口与《环境基线》一致）；
2. 补装 Python 依赖 `flask`、`flask-cors`、`pandas`、`pyarrow`（无外网用《06》§10.1 的离线 wheels）；
3. 装 Node 18+ 并构建前端：`npm ci`（或 `npm install`）→ `npm run build`，产物由 Flask 静态托管；北京 GeoJSON 与 ECharts **本地内置、不走 CDN**；
4. 收齐 `handoff/{ods,dwd,dws,ads,forecast}`，逐包通过 `manifest.json`（行数 + `sha256` + `seed` + 生成主机名）校验，不通过即退回重传；
5. 拉取最新代码并合并各成员分支；
6. `start_part2.sh` / `stop_part2.sh` 跑通，连续两遍演示无人工干预；
7. 演示可达性：改用桥接网卡、服务监听 `0.0.0.0`、放行端口（如 `sudo ufw allow 5000/tcp`）；
8. 按《06》§10.2 资源基线调参（VM 内存 ≥ 8 GB 推荐 12 GB、磁盘 ≥ 60 GB、`spark.sql.shuffle.partitions=8`、executor/driver 各 2g）。

**（1.3）选择、唯一性与变更**：

- **选择标准**：优先选 **#2（TL）的机器**——收包校验、Flask、`start_part2.sh`、端到端联调等集成机操作本属其职责，机器与职责合一最省沟通；若该机资源不足，则改选 **VM 内存/磁盘最宽裕、最早 `check_env.sh` 全绿**的一台，但须保证 #2 能随时上机（本人使用或 SSH），避免 D2–D3 因「等人开机器」而卡住。
- **唯一权威**：全组只指定一台，避免两地各跑一遍导致 ADS 数据版本不一致、答辩口径不一；**D1 内确定**，由 #2 记入技术决策记录。
- **变更与容灾**：集成机可中途更换——数据与产物经 `handoff/` 包搬迁（含 `sha256` 校验）、代码经 git 拉取、HDFS 中间数据可由交接包重放，**无需重装环境**；且生成器固定随机种子、同种子重跑哈希一致，最坏情况可现场重建全部数据。切换后须重跑（1.2）检查清单并留一次全链路运行记录。

**（2）为什么不能靠 git 传数据**：ODS 原始数据、DWD/DWS/ADS 的 Parquet、MLlib 模型与 `metrics.json` 均为大文件且依赖 HDFS；git 只承载代码/脚本/SQL/文档。数据与产物统一通过 **VMware 共享文件夹 + 网盘备份** 交接，目录约定如下：

| 交接包 | 内容 | 责任流向 |
|---|---|---|
| `handoff/ods` | ODS 原始 CSV/JSON（含脏数据）+ `injection_log.json` + 行数清单 | #4 → #3 |
| `handoff/dwd` | 清洗后 `dwd_*` / `dim_*` Parquet + 清洗报告 | #3 → #4 / #5 |
| `handoff/dws`、`handoff/ads` | DWS 汇总与 ADS 指标 Parquet + 导出 JSON | #4 → #2 / #1 |
| `handoff/forecast` | MLlib 模型、`metrics.json`、预测结果 | #5 → #2 |

**（3）交接包规范（必须遵守）**：

- 从 HDFS 导出：`hdfs dfs -get /ev-charging/<layer>/... ./handoff/<layer>/`（Parquet 目录直接整体拷贝；大表另打 `tar.gz`）。
- 每个交接包根目录必须含 `manifest.json`：各文件行数、`sha256` 校验和、生成 `seed` / `run_id`、生成时间、生成机主机名。
- 接收方必须通过校验（`sha256sum -c checksums.txt` 或 `handoff/verify.py`）后才可开工；校验不通过即退回重传。
- 数据包**不进 git**（写入 `.gitignore`）；网盘仅作冗余备份，以共享文件夹为准。

## 3. Flask 后端设计

### 3.1 工程结构

```text
server/
├── app.py                 # 入口：注册蓝图、统一 CORS、errorhandler
├── config.py              # ADS 数据库路径、缓存刷新间隔、分页上限
├── common/
│   ├── db.py              # sqlite3 只读连接 + query() + _norm() 归一化
│   └── resp.py            # ok() / fail() 统一响应封装
├── services/
│   ├── ads_reader.py      # 读取 ADS 结果（SQLite `ads.db` 只读）并缓存
│   └── forecast_reader.py # 预测结果读取（选做）
├── blueprints/
│   ├── overview.py        # /api/overview/*
│   ├── user.py            # /api/user/*
│   ├── station.py         # /api/station/*
│   ├── enterprise.py      # /api/enterprise/*
│   ├── gov.py             # /api/gov/*
│   ├── quality.py         # /api/quality/*
│   └── forecast.py        # /api/forecast/*（选做）
└── requirements.txt       # flask、flask-cors（SQLite 用标准库，无需额外驱动）
```

### 3.2 数据供给策略

- **说明（结果物化）**：ADS/DWS 结果由 SparkSQL 作业**物化**落盘，Flask 只读文件/库、不在请求内起 Spark；这是刻意的性能取舍（保证接口响应），**分层计算仍全部由 SparkSQL on Hadoop 完成**，不是绕过 SparkSQL。
- **ADS 落地 = SQLite 单文件**（`ads.db`）：ADS 各指标表在 HDFS 以 Parquet 物化后，导出为 SQLite 单文件（与第一阶段数据库端同为 SQLite，口径与工具链延续）；Flask 用 Python 标准库 `sqlite3` **只读**打开，进程启动时加载、定时（60s）重载。**零额外依赖**：不需要 MySQL 服务、不需要 JDBC 驱动，无外网虚拟机也能跑。
- 需要明细查询的接口（如单站利用率曲线）读取 `ads.db` 中的汇总/明细表，避免请求级 Spark 作业。
- 缓存带 `generatedAt`，前端据此显示数据时间；`ads.db` 缺失或表为空时返回 `code != 0` 与明确错误，前端展示「数据已过期」。
- **SQL 安全**：排序/筛选字段一律走**白名单映射**（`SORTABLE` 字典），**禁止**把前端参数直接拼进 SQL（如 `ORDER BY {sort}`）。

### 3.3 API 一览（与 #1 共同冻结，D2 前）

统一返回：`{ "code": 0, "message": "ok", "data": ..., "generatedAt": "ISO8601" }`

| 命名空间 | 接口 | 数据来源（ADS/DWS） |
|---|---|---|
| `/api/overview` | `kpis`、`stations`、`charger-status`、`load-24h`、`events` | `ads_revenue_overview`、`ads_charger_health`、DWS 负荷 |
| `/api/user` | `price-compare`、`price-distance`、`idle-ranking`、`peak-heatmap` | `ads_station_ranking`、`ads_peak_hour` |
| `/api/station` | `{id}/utilization`、`{id}/mix`、`{id}/health`、`coverage` | `dws_station_day`、`ads_charger_health` |
| `/api/enterprise` | `revenue-trend`、`station-ranking`、`user-growth`、`user-rfm`、`monthly` | `ads_revenue_overview`、`ads_user_profile_rfm` |
| `/api/gov` | `coverage`、`service-stats`、`carbon`、`peak-load`、`utilization` | `ads_gov_service` |
| `/api/quality` | `summary`（清洗前后行数、10 类问题检出/注入对账） | `quality_report.json`、`cleaning_report.json` |
| `/api/forecast`（选做，按必做完成） | `24h`、`recommend`、`metrics` | `ads_forecast_result` |

口径约束：金额为整数分、时间为 `+08:00` ISO 8601、在线率/利用率沿用第一阶段定义，后端不重定义业务口径。

### 3.4 工程规范（借鉴老师参考实现 `flask_api.py`）

老师下发的 `flask_api.py`（见桌面）虽绑定 MySQL 与教师侧 ADS 表，但其中**以下 7 条工程做法已在其环境中验证过**，本项目直接沿用（改造为 SQLite 版）：

| # | 规范 | 做法 | 收益 |
|---|---|---|---|
| 1 | **统一响应封装** | `ok(data)` / `fail(msg, code, status)` 统一返回 §3.3 契约结构（老师额外带 `total`，本项目暂不用，理由见下） | 前端只需判 `code`；错误也是结构化 JSON |
| 2 | **统一异常处理** | `@app.errorhandler(404)` / `(500)` 统一走 `fail(...)` | 前端不会收到 HTML 错误页导致解析失败 |
| 3 | **统一 CORS** | `@app.after_request` 统一加 `Access-Control-Allow-Origin` 等响应头 | 前端跑在另一端口也能直接调用 |
| 4 | **排序字段白名单** | `SORTABLE = {前端值: 真实列名}`，取不到就回落默认值；**禁止** `ORDER BY {用户输入}` | 防 SQL 注入（落实 §3.2「SQL 安全」） |
| 5 | **聚合值归一化** | `_norm()` 把 `Decimal`/`bytes` 转成 int/float/str；`COUNT`/`SUM` 一律 `COALESCE(...,0)` | 避免序列化失败与 `None` 参与除法报错 |
| 6 | **参数安全解析** | `get_int(name, default, lo, hi)` 带上下限钳制（如 `size` ≤ 200） | 防超大分页拖垮服务 |
| 7 | **中文与字段顺序** | `app.json.ensure_ascii = False`、`app.json.sort_keys = False` | 中文不转义成 `\uXXXX`，字段顺序稳定 |

**与老师实现的差异（不可照抄）**：

| 项 | 老师 `flask_api.py` | 本项目 |
|---|---|---|
| 数据源 | `pymysql` 连 MySQL（`charging_dw` 库） | 标准库 `sqlite3` 只读 `ads.db`（见 §3.2） |
| ADS 表 | `ads_station_stat`、`ads_user_level_stat`、`ads_facility_stat`、`ads_period_stage_stat`、`ads_battery_health_stat` | 见 §3.3（表名与口径均不同） |
| 接口组织 | 扁平 `/api/v1/*` | 按「用户 / 站点 / 企业 / 政府」四类视角分命名空间 |
| 响应字段 | `msg` + `total` | §3.3 冻结的是 `message` + `generatedAt` |

> ✅ **已定（接口契约结论，D2 接口冻结会直接沿用）**：响应字段名**采用本组自己的契约**——`{"code": 0, "message": "ok", "data": ..., "generatedAt": "ISO8601"}`，**不采用老师的 `msg`**。该契约在《01》§4 与本文 §3.3 两处已一致，前端照此实现即可，无需再改其它文档。
>
> **关于老师的 `total`（本项目不引入）**：`total` 是「总记录数」，**只在分页接口里才有意义**（`data` 仅返回当前页时，前端要靠它算总页数）。本项目接口以**全量返回 / Top-N 排行**为主（站点仅 8 个、月度汇总 12 条、用户侧为 RFM 聚合指标），**没有分页接口，故不引入 `total`**；前端需要"共几条"时用 `data.length` 即可。
>
> 若后续确实新增了分页接口：按本表第 1 条补上 `total`，并**同步修改《01》§4 与本文 §3.3 两处契约**（两处必须一起改，否则前后端对不上）。

## 4. 集成职责（TL）

1. **接口冻结**：D2 前与 #1 冻结 API 字段清单；与 #4 冻结 ADS 表结构；之后只允许兼容新增。
2. **集成链路**：`生成器(#4) → HDFS(#2) → 质量/清洗(#3) → 数仓(#4) → Flask(#2) → 前端(#1)`，D3 完成端到端联调。
3. **启动脚本**：提供 `start_part2.sh`（HDFS/YARN → Spark 作业 → Flask → 前端静态托管）与 `stop_part2.sh`，**在集成/演示机上执行**，用于答辩一键复位演示；执行前需确认 `handoff/` 各交接包已就位并通过校验（拉伸目标：可复制到任意成员机执行）。
4. **环境与数据交接**：维护《环境基线》+《部署手册》+`check_env.sh` 三件套；主持 `handoff/` 交接包的交接与校验，登记每次交接的 `run_id` 与接收人。
5. **技术决策记录**：记录部署形态（伪分布式、5 台互不组网）、集成机选择、数据交接方式、降级方案等决策及理由。
6. **参考实现改造**：老师下发的四层 demo（`demo_ods/dwd/dws/ads.py`、`dw_common.py`）为 **`local[*]` + 本地 CSV** 写法，仅作分层思路与口径参考；本项目全部作业须改造为 **Spark on YARN + HDFS 读写**（改造点见《06》§9.5）。老师另下发的 `flask_api.py` 绑定 MySQL，其工程做法已整理进 §3.4，代码本身须改为 `sqlite3` 版。

## 5. 排期（3 天，对应总体排期 D1–D3）

| 日 | 任务 |
|---|---|
| D1 | 各成员机 Hadoop 伪分布式安装完成（`check_env.sh`）、集成机确定；Spark 安装并与 HDFS 打通；输出《环境基线》《部署手册》与自检脚本 |
| D2 | Flask 工程搭建；全部 API（overview / user / station / enterprise / gov / quality / forecast）完成并与前端联调 |
| D3 | 端到端集成、`start_part2.sh`、性能与缓存验证；晚间修复、冻结、彩排 |

## 6. 验收标准

**成员机级（每台 VM 各自自测）**

1. 按《06-TL-Hadoop伪分布式安装部署指南》部署后 `check_env.sh` 全绿：`jps` 五进程齐全，HDFS/YARN Web UI 可访问，版本/路径/端口与《环境基线》一致。
2. 本模块作业（生成入库 / 清洗 / 分层 / 预测）在本机伪分布式 Hadoop 上以 Spark on Hadoop 方式跑通，可出示 YARN 任务记录。

**集成/演示机级（全链路）**

3. `handoff/` 各交接包 `manifest.json` 校验通过（行数与 `sha256` 一致），接收方可复算。
4. API 全部按冻结契约返回，数值与 ADS 表一致，异常时返回结构化错误。
5. `start_part2.sh` 在集成机一键拉起全栈，连续两遍演示无人工干预。
6. 如实说明部署形态为「单机伪分布式、5 台互不组网」，不宣称多节点集群。

## 7. 风险与降级

| 风险 | 应对 |
|---|---|
| 5 台 VM 环境不一致，出现「我机能跑、他机不能跑」 | 以《环境基线》锁定 JDK/Hadoop/Spark/Python 版本、路径与端口；官方 tarball 与校验和组内共享；每台部署后跑 `check_env.sh` 自检 |
| 共享文件夹/网盘传输大文件慢或中断 | 交接包统一压缩（Parquet/`tar.gz`）+ `manifest.json` 校验和；网盘冗余备份；校验失败即退回重传 |
| 伪分布式被质疑「不是分布式」 | 文档与答辩如实说明伪分布式形态，强调具备完整 NameNode/DataNode/YARN 体系且作业以 Spark on Hadoop 运行 |
| 虚拟机内存不足导致 YARN 作业失败 | 调低 executor 内存与并行度；必要时 Spark local 模式连 HDFS 过渡，答辩前恢复 YARN |
| ADS 读取慢 | 结果文件化 + 内存缓存，禁止请求级 Spark 作业 |
