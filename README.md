# BIT-SummerTermProject
构思小学期项目

可参考github仓库：
- https://github.com/Robin-WZQ/Charla

## 当前设计基线（2026-09-01）

- [总体设计说明](docs/plans/2026-09-01-ev-charging-platform-design.md)
- [实施计划索引](docs/superpowers/plans/README.md)
- [交互式五系统架构图](docs/design/five-system-architecture.html) ———— 格式为html源文件，下载下来后直接使用浏览器打开即可看到内容

> 当前设计基线已归档，五端按冻结接口并行实施；各模块是否达到现场联调门槛以对应测试报告为准。

## 分工

| 编号 | 正式角色 | 管理职责 | 开发模块 |负责人 |
|------|----------|----------|----------|----------|
| #1 | PM | 范围、排期、风险、答辩组织 | Web 大屏 | 王浩恩 |
| #2 | TL | 架构、接口、技术决策、集成 | Qt 管理/服务端 | 杨佳车 |
| #3 | PRL | 同行评审、缺陷把关、验收签字 | Qt 用户端与腾讯地图 | 胡晟源 |
| #4 | SCML | Git、配置项、版本、发布包 | SQLite 与模拟器 | 倪宇骏 |
| #5 | PE | 模块开发、模型实验与指标说明 | ML | 庞项祯 |

## Qt 用户端

环境配置、构建测试、离线行为和答辩演示步骤见 [用户端 README](apps/user-client/README.md)。
