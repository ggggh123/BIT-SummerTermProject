# #3 PRL 开工验证记录（2026-09-14）

## 结论与边界

本机 Ubuntu 25.04 已完成全局工具链安装，基础代码及 HDFS／YARN 运行链路验证通过。**本轮是开工基础，不是十类清洗规则、正式 DWD 或第二阶段整项目验收。Ubuntu 22.04 尚未复验。**

工作区：`/mnt/hgfs/Desktop/SummerTermProject/worktrees/part2-prl`；本地分支 `feat/part2-prl`，基于远端 dev `1bd4b89f4049b3b735fd2bc5d29e40092cc12bba`。本轮未推送远端，未修改第一阶段其他工作区。

> **说明（2026-09-15 补注）**：上面记录的是当时那台机器的实际工作树路径，属**历史留档**，其他人不必照此修改——仓库内脚本一律从自身位置推导根目录（如 `run_pipeline.sh` 的 `script_dir/../..`），在任何机器上 `cd` 到自己的仓库根即可。
> 该记录中「**Ubuntu 22.04 尚未复验**」这一结论已于 2026-09-15 由集成机（`niyujun01`，Ubuntu 22.04.3）完成复验：使用同一份 ODS 交接包（seed `20260914`，1,162,576 行）在 Spark on YARN 上产出 DWD 7 表，详见 `part2/plans/10-主线整合记录.md` §4 与 `part2/docs/evidence/2026-09-15-integration-host-ubuntu2204.json`。

## 已验证

| 项目 | 结果／证据 |
|---|---|
| 原设计资料 | 7 份正文导入；与外层原文件用忽略 CRLF 的 diff 比对一致，原文件未改 |
| 基础测试 | **30 项全部通过**，包括确定性样本、金额／时间、状态空值、误报／漏报、交接篡改、配置拒绝覆盖 |
| 全局运行时 | Java 8u504、Python 3.10.21、Hadoop 3.2.1、Spark／PySpark 3.5.7 |
| 系统 Python | 仍为 3.13.3；新 Python 在固定全局目录，不是 venv |
| 全局导入 | 普通 `python3.10` 可导入 PySpark 3.5.7 与 Py4J 0.10.9.7 |
| Hadoop | NameNode、DataNode、SecondaryNameNode、ResourceManager、NodeManager 五进程均运行 |
| HDFS／YARN | HDFS 业务目录可读；YARN REST API 为 STARTED |
| Spark 作业 | 两次真实 YARN 作业均完成原始数据读取、Python worker 执行、Parquet 写入与读回 |
| 重复启动 | 第二次启动保留五个已有进程，没有重复格式化 |
| 停止 | 停止宽限调至 20 秒后复验通过；8020、9870、8088、8042 无监听，数据保留 |
| 静态检查 | Python 编译检查、Shell 语法检查、git diff --check 通过 |

## Spark 批次

第一次：`application_1789358512765_0001`，master=yarn，7 张表／24 行；worker 验证到 Python 3.10。

第二次：`application_1789358753550_0001`，master=yarn，7 张表／24 行；driver 和两个 worker 均严格验证为 Python 3.10.21。YARN CLI 确认 FINISHED／SUCCEEDED、进度 100%。

输出：`hdfs://localhost:8020/ev-charging/quality/_smoke/prl-smoke-20260914T040636Z-15587/profile`。

此输出为原始字段画像，明确标记 `environment-smoke-not-dwd`。测试输入为 `prl-test-fixture`，不是 #4 的正式数据。没有以注入日志充当检测结果。

小型证据已纳入本工作区：

- [30 项测试原始输出](evidence/2026-09-14-unit-tests.txt)
- [最终本机环境自检 JSON](evidence/2026-09-14-runtime.json)
- [第二次 YARN 作业结果 JSON](evidence/2026-09-14-yarn-smoke.json)

完整本机日志保留于 `/home/hushengyuan/ev-part2/evidence/`。第二次完整 Spark 日志为 `prl-smoke-20260914T040636Z-15587/spark.log`，SHA-256 为 `7b55208ad1a516ff19b04e78dfbde1414af39350f0fc3a2bb12994f1c0ce78fe`。

## 已处理与保留说明

- 首轮 NodeManager 在默认 5 秒停止时限后被 Hadoop 强制终止；已增加至 20 秒，最终重新启停没有再出现该强制终止提示。
- Spark 提示 native-hadoop 加速库不可用，使用 Java 实现；本轮 HDFS／Parquet 读写均通过。此结果不代表已验证大规模性能。
- Hadoop 3.2.1 对 YARN_* 兼容环境变量打印弃用提示，不是作业失败。
- 运行过程中未下载 Ubuntu 镜像、未更换软件源、未改系统主机名或 SSH 登录设置。
- 工具本体约 1.62 GiB，下载缓存约 523 MiB；均在 Linux 磁盘，不提交 Git。

## 待完成

1. #2／#4／#5 确认 v0.1 契约与本机环境例外。
2. 实现十类 Spark 检测／清洗规则、处置审计及 DWD 输出。
3. 接入 #4 正式数据并做全量性能、金额与层间对账。
4. 对接 #1／#2 的质量报告展示接口。
5. 在真正 Ubuntu 22.04 集成机复验；演示前重跑样例生成当次 YARN 记录。
