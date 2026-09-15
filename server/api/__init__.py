"""Flask 蓝图包：按契约 §2–§7 的命名空间拆分，全部挂 `/api` 前缀。

| 模块 | 命名空间 | 角色 | 契约节 |
|---|---|---|---|
| `overview` | `/api/overview` | 主页（#1 王浩恩） | §2 |
| `quality`  | `/api/quality`  | 数据质量对账（#3） | §2 |
| `enterprise` | `/api/enterprise` | 企业视角 | §3 |
| `user` | `/api/user` | 用户视角 | §4 |
| `station` | `/api/station` | 充电站视角 | §5 |
| `gov` | `/api/gov` | 政府视角 | §6 |
| `forecast` | `/api/forecast` | 预测（#5） | §7 |
"""
