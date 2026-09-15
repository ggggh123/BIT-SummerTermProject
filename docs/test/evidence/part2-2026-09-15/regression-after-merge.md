# 合并 #4/#5 最新提交后的回归（2026-09-15）

> 触发：把 `origin/feat/part2_SCML`（#4，`13b18f0` 正式规模校验）与 `origin/feat/part2-ml`（#5，`53b2dd4` ML 预测批次 + 契约校验脚本）合进 `feat/part2-integration`
> 合并结果：**两处零冲突**（合并提交 `da9d0d1` / `9af5d99`），工作区干净
> 运行环境：Windows（Python 3.13 + `.venv-win`：flask 3.1.3 / flask-cors 6.0.5；Node 24.15.0）+ VM `TimeMachine`（Flask 托管 `web/dist` + `/api/*`）
>
> 🔄 **演示机迁址提示（2026-09-15 晚）**：团队演示机已由 `TimeMachine`（192.168.88.131）迁至 `niyujun01`（192.168.59.128）。
> **本文是迁址前的历史回归记录，原文保留不改**；新演示机地址以 `docs/management/part2-defense-evidence.md` 与 `part2-metric-checklist.md` 为准。

## 回归结果（8 项全绿）

| # | 项目 | 命令 | 结果 |
|---|---|---|---|
| 1 | 前端单测 | `node --test web/tests/*.test.mjs` | **36 / 36 通过** |
| 2 | 第一阶段回归 | `node --test dashboard/tests/*.test.mjs` | **36 / 36 通过** |
| 3 | 后端契约自检 | `python server/tools/selftest_contract.py` | **55 项通过，失败 0** |
| 4 | 前端字段对齐 | `python server/tools/selftest_frontend_shape.py` | **46 项比对，不兼容 0 处** |
| 5 | 静态托管 | `python server/tools/selftest_static_dist.py` | `[OK]` |
| 6 | #4 正式规模校验（本次新增） | `python part2/scml/scripts/check_scml_delivery.py --ods handoff/ods --dws handoff/dws --ads handoff/ads --require-full` | `SCML delivery: ready`；`[OK] scale: formal delivery scale is confirmed`；`[OK] reconcile`；`[SKIP] dwd`（#3 未交付） |
| 7 | #5 预测契约校验（本次新增） | `python part2/tmp/check_db.py handoff/ads/ads.db` | 三张预测表列集**逐列一致**：`ads_forecast_batch` 1 行 / `ads_forecast_24h` 144 行 / `ads_forecast_metric` 24 行 |
| 8 | SCML 单测（4 个文件） | `python part2/scml/tests/test_{ads_schema_contract,dws_ads_pipeline,dws_schema_contract,generator_contract}.py` | **38 / 38 通过**（7 / 18 / 6 / 7） |
| 9 | 跨机五页渲染 | Windows 无头 Chrome → `http://192.168.88.131:5000` | 主页 **5** 图 / 用户 **4** / 充电站 **4** / 企业 **4** / 政府 **3**；KPI 仍为 `¥4,758,207.37`、`3,972,088.20 kWh`、企业页 `¥1,583,793.74`（与合并前一致，无回归） |

## 一个已知问题（测试自身的跨平台缺陷，非产品缺陷）

`part2/scml/tests/test_delivery_check.py` 在 **Windows 默认控制台编码**下 3 个用例全部报错：

```text
UnicodeDecodeError: 'utf-8' codec can't decode byte 0xbd in position 11: invalid start byte
TypeError: unsupported operand type(s) for +: 'NoneType' and 'str'   # build.stderr + build.stdout
```

原因：`setUp` 用 `subprocess` 跑生成器，子进程输出是 GBK 中文，父进程按 UTF-8 解码失败 → `stdout` 变 `None` → 拼断言消息时崩溃。设 `PYTHONIOENCODING=utf-8` 后 **`Ran 3 tests — OK`**。

给 #4 的建议（一行）：`subprocess.run(..., encoding="utf-8", errors="replace")`，或显式给子进程传 `env={"PYTHONIOENCODING": "utf-8", ...}`，这样 Windows 上也能直接跑。

## 仍待接的数据（不是回归项）

`ads_forecast_batch` 当前仍是 `baseline-2026-09-15 / seasonal-naive-baseline / is_baseline=1`。合并 #5 的代码不会自动替换批次数据——要让大屏显示真实 MLlib 预测，需要把 #5 的 `handoff/forecast` 批次合入 `handoff/ads/ads.db`（替换 `ads_forecast_24h` / `ads_forecast_metric` / `ads_forecast_batch` 三表），这一步要与 #5 对接。
