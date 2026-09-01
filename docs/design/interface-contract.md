# Foundation v1 接口合同

## 范围

本文档冻结 Foundation 阶段的共享协议。用户端、管理/服务端、模拟器、Web 大屏和 ML 模块都应复用 `shared/contracts` 与 `shared/protocol`，不要在各自模块重复维护 action、status 或 permission。

## 帧格式

```text
0               4
+---------------+--------------------------+
| uint32 length | UTF-8 JSON payload bytes |
+---------------+--------------------------+
```

- `length` 使用 4 字节无符号大端整数。
- `length` 不能为 0。
- 最大 payload 为 `1,048,576` 字节。
- payload 是一个 UTF-8 JSON 对象。

## RequestEnvelope

```json
{"version":1,"requestId":"fixture-health","action":"system.health","token":"","payload":{}}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `version` | number | 是 | 当前只接受 `1` |
| `requestId` | string | 是 | 非空请求 ID |
| `action` | string | 是 | 见 action 列表 |
| `token` | string | 否 | 匿名请求为空字符串 |
| `payload` | object | 是 | 业务参数 |

## ResponseEnvelope

```json
{"requestId":"fixture-health","ok":true,"code":"OK","message":"healthy","data":{"status":"ok"}}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `requestId` | string | 是 | 对应请求 ID |
| `ok` | boolean | 是 | 是否成功 |
| `code` | string | 是 | 错误码或 `OK` |
| `message` | string | 是 | 人类可读信息 |
| `data` | any | 是 | 成功数据或空对象 |

## 约定

- JSON 使用 camelCase。
- SQLite 列名使用 snake_case。
- 金额使用整数分。
- API 时间使用 ISO 8601，并带 `+08:00`。
- 运行数据使用合成、确定性数据，不声明为真实平台数据。

## 状态常量

| 类型 | 取值 |
| --- | --- |
| user | `active`, `frozen` |
| charger | `idle`, `reserved`, `charging`, `fault`, `restarting` |
| order | `reserved`, `charging`, `completed`, `cancelled` |
| congestion | `low`, `medium`, `high` |
| forecast run | `active`, `superseded` |

## Action 常量与权限

| action | 允许身份 |
| --- | --- |
| `auth.user_login` | anonymous |
| `user.get` | user |
| `user.update` | user |
| `wallet.recharge` | user |
| `station.list` | user/admin |
| `station.detail` | user/admin |
| `charger.list` | user/admin |
| `charge.reserve` | user |
| `charge.start` | user |
| `charge.stop` | user |
| `charge.settle` | user |
| `order.current` | user |
| `order.list` | user |
| `order.cancel` | user |
| `admin.login` | anonymous |
| `admin.dashboard` | admin |
| `admin.station_create` | admin |
| `admin.charger_restart` | admin |
| `admin.user_list` | admin |
| `admin.user_set_status` | admin |
| `telemetry.push` | simulator |
| `simulator.fault_set` | simulator |
| `simulator.status` | simulator |
| `forecast.publish` | ml |
| `forecast.latest` | user/admin |
| `system.health` | anonymous/user/admin/simulator/ml |
| `demo.reset` | admin |

## 错误码

| code | 含义 |
| --- | --- |
| `OK` | 成功 |
| `INVALID_REQUEST` | JSON、字段类型或必填字段错误 |
| `UNSUPPORTED_VERSION` | `version` 不是 `1` |
| `AUTH_REQUIRED` | 缺少 token |
| `FORBIDDEN` | 身份无权调用该 action |
| `INVALID_PHONE` | 手机号格式错误 |
| `INVALID_CREDENTIALS` | 登录凭据错误 |
| `USER_FROZEN` | 用户被冻结 |
| `ACTIVE_ORDER_EXISTS` | 用户已有活动订单 |
| `CHARGER_NOT_AVAILABLE` | 充电桩不可用 |
| `ORDER_STATE_CONFLICT` | 订单状态不允许当前操作 |
| `INSUFFICIENT_BALANCE` | 余额不足 |
| `MAP_API_ERROR` | 腾讯地图调用失败 |
| `FORECAST_INVALID` | 预测批次校验失败 |
| `FORECAST_STALE` | 预测已过期且调用方要求新鲜预测 |
| `SERVER_BUSY` | 服务端繁忙 |
| `DB_BUSY` | SQLite 忙或锁等待超时 |
| `INTERNAL_ERROR` | 未分类服务端错误 |

## 载荷摘要

27 个动作的 payload 和 success data 以 `docs/plans/2026-09-01-ev-charging-platform-design.md` 第 8.3 节为准。Foundation 代码只冻结动作名、状态名、权限矩阵、帧协议和 JSON envelope；具体业务字段由后续模块在不改变 v1 基础协议的前提下补测试和实现。

## 变更规则

2026-09-02 18:00 后，协议字段只能以向后兼容方式新增，不得改名、删除或改变含义。
