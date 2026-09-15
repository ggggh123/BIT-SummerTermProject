# 集成分支在 TimeMachine 虚拟机上的全栈实测（2026-09-15）

> 执行人：#1 王浩恩（PM）｜目标：把 #2 的一键脚本与集成分支在**本组虚拟机**上实测（答辩硬闸门：单进程起全栈、两次无人工干预、Windows 可直接看大屏）
> 机器：`TimeMachine`（Ubuntu 25.04 / JDK 17 / Hadoop 3.4.1 / Spark 3.5.7），共享目录 `/mnt/hgfs/BIT-SummerTermProject` = 仓库根
> 代码：`feat/part2-integration` 合入 `origin/feat/part2-02`（`f744601`）后，含 `part2/scripts/{start,stop}_part2.sh` 与 `part2/plans/00–08`
> 数据：`handoff/ads/ads.db`（正式规模，287 桩 / 112,422 单，`runId=ads-20260914194230`）

## 1. 本机前置事实（与 #2 集成机不同，导致脚本原样不可用）

| 项 | 本机（TimeMachine） | #2 集成机（niyujun01，见 `08-集成验证报告.md`） |
|---|---|---|
| 操作用户 / sudo | `spiderboy`；`sudo` **需要密码**（非免密） | `bit` + `sudo -u hadoop` |
| 仓库位置 | `/mnt/hgfs/BIT-SummerTermProject` | `~/part2`（目录取用，非克隆） |
| `python3` | 系统 3.13，**没有 flask**；依赖装在 `~/venvs/part2`（3.11，flask 3.1.3 + pyspark 3.5.7） | 系统 python3 可用 |
| `curl` | **未安装**（有 `wget` / `busybox`） | 有 |
| `/etc/profile.d/ev-second-project.sh` | **不存在**（本机是 `part2-env.sh`） | #2 建了软链接兼容 |
| HDFS/YARN | 由本人在本次会话用 `start-dfs.sh` / `start-yarn.sh` 启动，五进程齐（:8020/:8032/:8088/:9870 监听） | — |

## 2. A) #2 脚本原样运行：三个断点（原始输出摘录）

```text
===== 第二阶段一键启动 =====
[1/4] 启动 Hadoop（HDFS + YARN）...
  守护进程: 0/5                      ← 断点 1：sudo -u hadoop 在本机不可用（无 hadoop 用户 + sudo 要密码）
[2/4] 检查 ADS 库 ...  OK: 5.5M ...
  OK: 前端产物 web/dist 已就绪（1.3M）
[3/4] 启动 Flask 服务 ...
start_part2.sh: 行 66: curl: 未找到命令   ← 断点 2：健康检查用 curl，本机未安装
  !! 启动失败，请查看 logs/flask.log
ModuleNotFoundError: No module named 'flask'  ← 断点 3：脚本用系统 python3(3.13)，flask 在 venv(3.11)
```

外加两处需要环境变量兜底才能跑：`PART2_ROOT` 默认 `/home/bit/part2`、`ADS_DB` 默认 `$ROOT/part2/scml/handoff/ads/ads.db`（本机该路径不存在，库在 `handoff/ads/ads.db`）。

**结论**：脚本的骨架（HDFS/YARN → 数据检查 → Flask 托管 dist → 打印访问地址）是对的，但把集成机的三件环境事实写死了，换机器即失效。

## 3. B) 等价方式起全栈（未改动 #2 的脚本）：两遍均可重复

关键差异只有一条：用 venv 的 python 起服务；HDFS/YARN 已在运行，故第 1 步跳过。

```bash
export ADS_DB=/mnt/hgfs/BIT-SummerTermProject/handoff/ads/ads.db
cd /mnt/hgfs/BIT-SummerTermProject
ADS_DB=$ADS_DB PORT=5000 setsid nohup ~/venvs/part2/bin/python server/app.py \
  > /tmp/part2logs/flask.log 2>&1 < /dev/null &
```

两遍（第二遍先 `pkill -f "server/app.py"` 再起）结果一致：

| 检查 | 第一遍 | 第二遍 |
|---|---|---|
| `/api/health` | `"status":"up"`，`runId=ads-20260914194230`，`apiRouteCount=28/27` | 同 |
| `/`（大屏首页） | 200 | 200 |
| `/api/overview/kpis` | 200 | 200 |
| `/geo/beijing.json` | 200 | 200 |
| 五条 hash 路由（`/`、`/#/user`、`/#/station`、`/#/enterprise`、`/#/gov`） | — | 全部 200 |
| 监听 / 进程 | `0.0.0.0:5000` | `0.0.0.0:5000`，`pgrep` 单一进程 |
| `jps` | 五进程齐 | 五进程齐 |

### 跨机（Windows → VM）验证

| 检查 | 结果 |
|---|---|
| `http://192.168.88.131:5000/` | 200（454 B，index.html） |
| `/api/overview/kpis` · `/api/overview/stations` | 200（287 B / 2118 B） |
| `/geo/beijing.json` | 200（101,117 B） |
| Windows 无头 Chrome 逐页抓 DOM | 主页 **5** 图、用户 **4**、充电站 **4**、企业 **4**、政府 **3**；主页 KPI `¥4,758,207.37` / `3,972,088.20 kWh` / `99.3%`，与接口逐位一致 |

即：**单进程 Flask 同时托管 `web/dist` 与 `/api/*`，Windows 浏览器可直接投影演示，两遍无人工干预** —— 形态要求达成；两遍之间需要人工执行的只有"停止旧进程"，这一步正是 `stop_part2.sh` 的职责。

## 4. 给 #2 的适配建议（最小改动，4 处）

```diff
-ROOT="${PART2_ROOT:-/home/bit/part2}"
+ROOT="${PART2_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
+PYTHON="${PART2_PYTHON:-python3}"            # 本机需指向 ~/venvs/part2/bin/python
+HADOOP_USER="${PART2_HADOOP_USER:-hadoop}"   # 本机为 spiderboy；sudo 需密码时应降级为"跳过并提示"
-  sudo -u hadoop bash -lc "jps" ...
+  sudo -n -u "$HADOOP_USER" bash -lc "jps" ... || true
+health() { if command -v curl >/dev/null 2>&1; then curl -s --max-time 10 "$1"; else wget -qO- --timeout=10 "$1"; fi; }
-  curl -s --max-time 10 "http://127.0.0.1:$PORT/api/health" | grep -q '"status": *"up"'
+  health "http://127.0.0.1:$PORT/api/health" | grep -q '"status": *"up"'
-  ... setsid nohup python3 server/app.py ...
+  ... setsid nohup "$PYTHON" server/app.py ...
```

`ADS_DB` 默认值也建议改为 `$ROOT/handoff/ads/ads.db`（与 `server/services/ads_reader.py` 的默认一致），或至少同时探测两个候选路径。

## 5. 连带发现

**5.1（给 #4）`/etc/profile.d/ev-second-project.sh` 缺失。** #4 的 `part2/scml/scripts/*.sh` source 该文件，本机只有 `part2-env.sh`；直接跑会拿不到 `JAVA_HOME/HADOOP_HOME/SPARK_HOME` → SparkSQL 链路（`run_dim_date.sh`/`run_dws_ads.sh`）在本机跑不起来。两条路：脚本改为 source `/etc/profile.d/part2-env.sh`，或本机建软链接（#2 在他机器上就是建软链接绕过的）。

**5.2（给 #4）venv 里没有 `pip`。** `~/venvs/part2/bin/python -m pip` 报 `No module named pip`（机器上有 `uv` 与 `python3.11`），如需在 venv 内装包要用 `uv pip --python ~/venvs/part2/bin/python` 或 `python3.11 -m venv --upgrade-deps`。PySpark 3.5.7 已在 venv 内，够用。

**5.3（演示观感，需 #4 决策）正式规模下桩状态快照几乎全是 `idle`。**

| 数据规模 | 状态分布 | 在线率 | 空闲桩 |
|---|---|---|---|
| 正式规模（`part2_scml_full.yaml`，本次 `ads.db`） | `idle 285` / `fault 2`（共 287） | **99.3%** | 285 |
| 1/10 规模（VM Spark 路线，`part2-2026-09-14/README.md`） | 含故障分布 | **76.7%** | 16 / 30 |

主页「桩状态环图」在正式规模下只剩两个扇区（且 99.3% 在线率对答辩而言不像真实运营数据）。建议生成器按站点利用率/故障注入比例生成状态快照列（`idle/reserved/charging/fault/restarting`），使环图与「利用率」「故障率」两张图自洽。

## 6. 复现方式

```bash
# Windows：ssh vm 已配好（~/.ssh/config 的 Host vm，密钥 ~/.ssh/vm_ed25519 与默认 id_ed25519 均已授权）
ssh vm "bash -l /mnt/hgfs/BIT-SummerTermProject/.local-tools/vm-start-test.sh"   # 起 Flask（venv python）
# 或按 §3 的三行手工起
```

> 本次会话结束时：VM 上 Flask 仍在 `0.0.0.0:5000` 运行（演示态），HDFS/YARN 五进程在跑；Windows 侧 `handoff/ads/ads.db` 与 `handoff/{ods,dws}` 为本地物化产物（114 MB，均在 `.gitignore` 内）。
