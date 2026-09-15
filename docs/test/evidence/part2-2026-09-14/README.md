# 第二阶段数据管道证据（1/10 规模，2026-09-14）

在 Ubuntu 虚拟机（主机名 `TimeMachine`）上以 **Spark on YARN** 跑通
`pipelines/part2/run_all.sh` 全部六步后的原始证据。

| 文件 | 说明 | 关键结论 |
|---|---|---|
| `reconcile_report.json` | 逐层对账（`reconcile.py` 输出） | **13/13 全绿**：DWS 营收合计 = DWD 金额合计（49,863,703 分，误差 0）、区域日汇总一致、粒度唯一、3 个站点-日抽样重算一致、ADS 站点营收合计一致 |
| `cleaning_report.json` | 清洗前后行数（`clean_to_dwd.py` 输出） | 订单 12,196→11,424、遥测 100,662→97,758、用户 500→488、站点 8→7（越界剔除）、小时表 17,280→17,251 |
| `quality_report.md` | 质量探查人读报告（`quality_check.py` 输出） | 10 类问题累计命中 4,179 条，与注入日志对账 R07–R10 完全一致 |

复现方式：

```bash
cd /mnt/hgfs/BIT-SummerTermProject/pipelines/part2
bash run_all.sh          # 六步：生成 → 上传 → 质量 → 清洗 → 分层 → 对账
```

YARN 应用记录可在 `http://<集成机>:8088/` 的 Applications 页核对（应用名分别为
`part2-quality-check`、`part2-clean-to-dwd`、`part2-build-warehouse`、`part2-reconcile`）。
