# 2026-09-15 证据：跨机演示态截图 + 抽验真值

> 采集人：#1 王浩恩（PM）｜数据源：`handoff/ads/ads.db`（正式规模，`runId=ads-20260914194230`，287 桩 / 112,422 单 / 90 天）
> 采集方式：VM（`TimeMachine`，192.168.88.131）上 Flask 单进程托管 `web/dist` + `/api/*`；Windows 侧无头 Chrome `--window-size=1920,1080 --screenshot` 逐页抓图
> 运行细节与脚本适配缺陷见同目录 [`vm-integration-run.md`](vm-integration-run.md)

## 1. 截图

| 文件 | 页面 | 路由 |
|---|---|---|
| `screenshots/01-home.png` | 综合运营大屏（主页） | `/` |
| `screenshots/02-user.png` | 用户视角 | `/#/user` |
| `screenshots/03-station.png` | 充电站视角 | `/#/station` |
| `screenshots/04-enterprise.png` | 企业视角 | `/#/enterprise` |
| `screenshots/05-gov.png` | 政府视角 | `/#/gov` |

## 2. 抽验真值（`docs/management/part2-metric-checklist.md` 第 6–15 行可直接抄录）

全部由 `ads.db` 直接查询得到；「与页面一致」一列是对上一轮无头 DOM 抓取结果的比对。

| # | 指标 | ADS 来源 | 真值（正式规模） | 与页面一致 |
|---|---|---|---|---|
| 6 | 近 30 日营收合计 | `ads_daily` 末 30 日 `revenue_fen` | **158,379,374 分 = ¥1,583,793.74** | ✓（企业页趋势显示 ¥1,583,793.74） |
| 7 | 站点营收 Top1 | `ads_station_day ⋈ ads_station` | **丰台区3号充电站 81,772,869 分 = ¥817,728.69** | 图表内，见 `04-enterprise.png` |
| 8 | RFM 分层人数 | `ads_user_rfm` | 一般挽留 1487 / 一般发展 851 / 重要保持 736 / **重要价值 605** / 重要挽留 419 / 一般保持 349 / 一般价值 304 / 重要发展 234 | 图表内，见 `04-enterprise.png` |
| 9 | 月度营收（最近月 2026-09） | `ads_daily` 按自然月 | **76,001,469 分 = ¥760,014.69**（06 月 ¥736,782.88、07 月 ¥1,650,215.14、08 月 ¥1,611,194.66） | ✓（企业页月度表 06/07 两行逐位一致） |
| 10 | 电价全市均值 | `ads_station.price_fen_per_kwh` | **120.0 分/度 = ¥1.20/度**（min 80 = ¥0.80，max 160） | ✓（用户页「电价最低 0.80 元/度」） |
| 11 | 各站空闲桩 Top1 | `ads_station.idle_cnt` | **36（8 站中 7 站并列）** | 图表内，见 `02-user.png` |
| 12 | 桩状态五类之和 | `ads_station` 五个计数列 | **idle 285 + reserved 0 + charging 0 + fault 2 + restarting 0 = 287**（= 总桩数，口径自洽） | ✓（5 图之一即环图） |
| 13 | 数据质量检出总数 | `ads_quality_issue` | **注入 32,775 / 检出 10,717**（逐类见下） | 页面「数据质量」面板 |
| 14 | 等效碳减排 | `ads_daily` 电量 × 0.581 t/MWh | **2,307.78 tCO₂**（`ads_district.co2_saved_kg` 合计 = 2,307.783 t，两算法一致） | 见 `05-gov.png` |
| 15 | 全城峰值负荷 | `ads_station_hourly` | 站点小时峰值 **1,381.7 kW**（海淀区2号站 2026-08-05T08:00）；全城按小时合计峰值 **730,103.5 kW @18 时** | 见 `05-gov.png` |

第 13 行逐类（`rule / 注入 / 检出 / 召回率`）：

```text
R01 缺失值      5600 / 5652 / 1.000     R06 金额口径错误   600 /  581 / 0.968
R02 重复记录   11200 /    0 / 0.000     R07 孤儿引用       360 /  684 / 1.000
R03 异常值      3360 / 3345 / 0.996     R08 非法字段值      16 /   16 / 1.000
R04 时间格式混杂 11200 /    0 / 0.000     R09 经纬度越界       1 /    1 / 1.000
R05 逻辑矛盾     412 /  412 / 1.000     R10 文本脏数据      26 /   26 / 1.000
```

## 3. 本轮暴露、需要他人闭环的问题

**3.1（#3 PRL）R02「重复记录」与 R04「时间格式混杂」注入 11,200 条、检出 0 条。** 这两类占注入总量（32,775）的 68%，召回率直接写 0 —— 答辩若被追问「十类问题都检出了吗」，这是硬伤。要么补齐检测规则，要么在报告里如实说明「ADS 侧口径不含这两类、由 #3 报告为准」，二者选一。

**3.2（#4 SCML）正式规模下桩状态快照几乎全是 `idle`。** `reserved/charging` 均为 0、`fault` 仅 2 → 在线率 99.3%、空闲 285/287；主页「桩状态环图」只有一个扇区，且 11 项「各站空闲桩」全部并列 36，图表没有区分度。建议生成器按站点利用率生成状态快照（与「利用率」「故障率」两图自洽）。

**3.3（#2 TL）一键脚本的跨机适配**（`part2/scripts/start_part2.sh`）：`PART2_ROOT` 写死 `/home/bit/part2`、`sudo -u hadoop`、依赖 `curl`、用系统 `python3`。本机（`spiderboy` / `/mnt/hgfs/...` / 无 curl / flask 在 venv）三处断点，详见 `vm-integration-run.md` §2 与 §4 的补丁建议。

**3.4（#4 SCML）`/etc/profile.d/ev-second-project.sh` 在本机不存在**，而 `part2/scml/scripts/*.sh` 会 source 它 → SparkSQL 链路在本机跑不起来（本机是 `part2-env.sh`）。

## 4. 状态快照修复验证（#4 `6cbbd11`，2026-09-15 10:12 复测）

#4 在 `6cbbd11` 里按站点利用率重塑了桩状态快照。重新生成 ODS + 物化 ADS 后（`runId=ads-20260915101224`）复测：

| 项 | 修复前 | 修复后 |
|---|---|---|
| 桩状态分布（287 桩） | `idle 285 / fault 2` | **`idle 197 / charging 61 / reserved 27 / fault 2`** |
| 主页「桩状态环图」 | 只有 1 个扇区 | **4 个扇区** |
| 各站空闲桩 | 8 站里 7 站并列 36 | **30 / 29 / 26 / 26 / 24 / 22 / 21 / 19**（有区分度） |
| 主页 KPI | — | `空闲桩 197`、订单 112,422、营收 ¥4,758,207.37（金额与订单未变，仅状态分布变化） |

同一轮修复还包含：`reconcile.py` 不再硬要求 `is_baseline=1`（真实 ML 批次可过）、`build_local.py` / `export_ads_db.py` 新增 `--forecast-handoff <path>` 用于接入 #5 的预测包、`test_delivery_check.py` 子进程输出加 `errors="replace"`（已验证：**不设 `PYTHONIOENCODING` 也 3/3 通过**）。

> 残余项：`onlineRate` 仍是 99.3%，因为故障桩仍只有 2 个——若答辩希望在线率更接近真实运营，需要 #4 调整故障注入比例（与状态快照是两件事）。

## 5. 补页面级 E2E 时发现的两个版面缺陷（#1，2026-09-15）

**5.1 主页内容被静默裁切（已修）**：`ScaleFrame` 原本把宿主高度固定为 `1080 × scale`，而 `.scale-host` 是 `overflow: hidden`；实测主页内容高约 **1632 设计 px**（1080 之外的是地图下半部、数据质量面板、事件流）。在 1920×1080 投屏上，`.scale-host` 可视高 960px，`scrollHeight` 1451px → **约 491px 被裁掉**，地图只露出 111px，数据质量面板整块不可见（而它正是老师要求里的展示项）。

修法：宿主高度改为按**实际内容高度**计算（`contentHeight * scale`）+ `.scale-host` 改 `overflow: visible`，并挂 `ResizeObserver` 跟随内容变化。**随后在 `feat/part2-web-layout` 里做了彻底方案（见 §7）**：主页重排为三列大屏栅格，内容压到 1080 设计高度以内。

**5.2 主页地图面板里的北京底图过小、8 个站点重叠**：面板只有 300px 高（601×300 画布），geo 受宽高比限制只能占约 **260px 宽**，站点像素集中在约 54×38 px 内 → 环图之外的这 8 个点挤成一团，像素级点击无法区分站点（实测点上 1 号站的位置命中到 8 号站）。

已做的缓解：给 `geo` 补 `layoutCenter/layoutSize`，`symbolSize` 上限由 30 压到 16。**根治同样依赖 5.1 的版面重构**（把地图放进主视区、拿到更大高度）。

E2E 落地：`web/e2e/dashboard.spec.mjs`（条数见 §7），`npm run test:e2e` 全绿（主页渲染、地图点击跳转并选中同一站、5s 轮询重绘、接口失败保留旧数据、DataV 渲染、版面）。

## 6. 补老师"开发环境/要求"的差距（2026-09-15 11:00）

老师 6 条要求逐条核对后，本轮补了三处：

**6.1 正式规模数据入 HDFS（第 2 条）**：用 #4 的 `hdfs_put_ods.sh` 把正式规模 ODS 推到 `/ev-charging/ods` —— **104.7 MB / 360 个 `dt=` 分区**，含 `manifest.json`、`injection_log.json`、`_SUCCESS`（脚本会先清空目标，昨天那批 1/10 规模扁平 CSV 已被清掉）；并把 `handoff/dws` 四个汇总表推到 `/ev-charging/dws`（3.9 MB + manifest + `_SUCCESS`）。`/ev-charging/dwd`、`/ads` 仍是 9-14 的旧数据，等官方 SparkSQL on YARN 路线（阻塞在 #3 的 `handoff/dwd`）。

**6.2 维度与对比分析对照表（第 3 条）**：新增 `docs/management/part2-analysis-dimensions.md` —— **15 个分析维度**（要求 ≥8）与 **5 组维度对比分析**（要求 ≥2：快充↔慢充、区↔区、站↔站、时段↔时段、用户分层↔分层），每行都给「来源表 → Flask 接口 → 大屏位置」，答辩可直接照念。同文档附版本基线表（Python 3.11.16 / Spark 3.5.7 / Hadoop 3.4.1 / Flask 3.1.3 / Node 24.15.0 / Vue 3.5.13）。

**6.3 DataV 大屏件（第 5 条）**：接入 `@kjgl77/datav-vue3@1.7.4`（DataV 的 Vue3 版，`web/package.json` 已声明 `engines.node >= 23`）。主页大屏：5 个图表面板换 `dv-border-box-8` 边框（封装 `DvFrame.vue`）、标题与 KPI 行加 `dv-decoration-10`、实时事件流换 **`dv-scroll-board`** 滚动榜单。E2E 从 4 条扩到 **5 条**（新增「DataV 大屏件已渲染」：断言边框盒 ≥5 且尺寸非零、滚动榜单存在且高度 >100px），`npm run test:e2e` **5/5 通过**；五页截图已按新样式刷新。

## 7. 主页版面重构（`feat/part2-web-layout`，2026-09-15）

按《01-PM》§3.1 的三列大屏栅格重排主页，目标是"1920×1080 一屏放下 + 地图进主视区中央"：

| 区域 | 内容 | 高度（设计 px） |
|---|---|---|
| 第 1 行 | KPI 四卡 | ~96 |
| 主视区（左 1.05fr / 中 1.6fr / 右 1.15fr） | 左：近 30 日营收趋势 + 桩状态（各 150 图表）｜**中：北京市站点分布（图表 380，列最宽）**｜右：利用率排行（150）+ 数据质量（列表内滚动 132） | ~450 |
| 底行（1.7fr / 1fr） | 24 小时负荷与预测（图表 220）｜实时事件流 ScrollBoard（220） | ~290 |

同时修了 `ScaleFrame` 的预留高度：原来固定预留 120px，而顶栏+页脚实际约 175px，导致整页比视口高约 55px、仍要滚一下；现在按**真实占位**（宿主顶部 + 页脚高度 + 12）预留。

实测（`web/e2e/dashboard.spec.mjs` 第 6 条，打 VM 真实栈）：`.scale-inner` 内容高 **≤1080 设计 px**、地图图表高 **≥380px**、整页 `scrollHeight ≤ 视口 + 32px`。E2E 由 5 条扩到 **6 条，全部通过**；五页截图已按新版面刷新（`screenshots/01-home.png` 由 203KB 变 266KB，内容更密）。
