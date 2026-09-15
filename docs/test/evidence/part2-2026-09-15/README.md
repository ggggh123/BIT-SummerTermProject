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
