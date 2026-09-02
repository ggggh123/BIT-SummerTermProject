# Foundation v1 冻结接口合同

## 1. 范围与规范词

本文是 Foundation v1 的自包含线协议与业务接口合同。用户端、Qt 管理/服务端、设备模拟器、Web 大屏和 ML 发布端必须共同遵守本文；不得在子系统内维护第二套 action、status、permission 或字段语义。

“必须”“不得”是强制规则；“可选”只表示字段可省略，不表示服务端可以为省略字段猜测权威值。本文中的 `object` 均为 JSON object，`array<T>` 为元素均符合 `T` 的 JSON array。

## 2. 帧与 JSON envelope

### 2.1 TCP 帧

```text
0               4
+---------------+--------------------------+
| uint32 length | UTF-8 JSON payload bytes |
+---------------+--------------------------+
```

- `length` 是 4 字节无符号大端整数，仅计算后续 JSON payload 字节，不包含自身。
- `length` 必须为 `1..1,048,576`；`0` 和大于 `1,048,576` 均为无效帧。
- payload 必须是 UTF-8 编码的一个 JSON object；TCP 拆包、粘包不得改变帧语义。

### 2.2 RequestEnvelope

```json
{"version":1,"requestId":"req-1001","action":"charge.reserve","token":"opaque-token","payload":{"chargerId":1001}}
```

| 字段 | JSON 类型 | 必填 | v1 规则 |
| --- | --- | --- | --- |
| `version` | integer | 是 | 必须恰为 `1`；其他数值返回 `UNSUPPORTED_VERSION`，缺失或非 number 返回 `INVALID_REQUEST` |
| `requestId` | string | 是 | trim 后非空；变更请求以它作为幂等键 |
| `action` | string | 是 | trim 后非空，必须是第 6 节的 27 个字符串之一 |
| `token` | string | 否 | 省略或空字符串表示匿名；不得记录到日志、响应或业务对象 |
| `payload` | object | 是 | 对应 action 声明的字段必须满足精确类型；未声明字段按下述规则忽略 |

所有需要认证的用户操作都从 token 对应会话取得 `userId`；自助操作 payload 不接受调用方提交的 `userId`。未声明字段不得成为权威写值；v1 服务端忽略它们，不得用它们绕过声明字段的校验或回显为服务端事实。

### 2.3 ResponseEnvelope

成功示例：

```json
{"requestId":"req-1001","ok":true,"code":"OK","message":"预约成功","data":{"order":{}}}
```

失败示例：

```json
{"requestId":"req-1001","ok":false,"code":"CHARGER_NOT_AVAILABLE","message":"充电桩当前不可预约","data":{}}
```

| 字段 | JSON 类型 | 必填 | v1 规则 |
| --- | --- | --- | --- |
| `requestId` | string | 是 | 正常服务响应必须与请求的非空 ID 相同；无法提取请求 ID 的帧/envelope 错误可为空字符串 |
| `ok` | boolean | 是 | 成功为 `true`，失败为 `false` |
| `code` | string | 是 | 成功恰为 `OK`；失败为第 5 节错误码且正常服务响应非空 |
| `message` | string | 是 | 人类可读；response parser 仅要求 string，允许空字符串 |
| `data` | any | 是 | 成功时必须是第 7 节规定的 object；失败时恰为 `{}` |

共享 response parser 对 `requestId`、`code`、`message` 只执行“必填且是 string”校验，因此可解析空字符串；这不降低正常服务端生成响应时的相关性和非空 `code` 要求。成功业务变更必须先提交事务，再发送 `ok=true`。同一 `requestId` 的同一变更重放返回已保存响应，不得重复扣款、充值、建单或发布批次。

## 3. 标量、可选值与权威性规则

- JSON `integer` 是数学整数，范围为 `-9,007,199,254,740,991..9,007,199,254,740,991`。ID 为正整数；计数、余额、订单金额和时长为非负整数；`amountFen`、`priceFenPerKwh` 为正整数。
- JSON `number` 必须有限，禁止 NaN 和正负 Infinity。纬度为 `[-90,90]`，经度为 `[-180,180]`；功率、电量、负载和距离为非负有限数。
- 金额一律为整数分（fen）。订单 `amountFen` 由服务端按累计电量和站点单价计算，客户端不得提交最终金额或结算后余额。
- `Timestamp` 是有效 ISO 8601 字符串，必须显式带 `+08:00`，形式为 `YYYY-MM-DDTHH:mm:ss[.fraction]+08:00`。`Date` 是有效的 `YYYY-MM-DD`。
- 可选字段缺失时必须省略；不得把缺失字段静默解释为权威默认。仅共享 schema 或 action 明确标为 `null` 的位置允许 JSON `null`。
- `mobile` 必须匹配 `^1[3-9][0-9]{9}$`。`nickname`、站点 `name/address`、`username/password`、`runId/modelVersion` 均为 trim 后非空 string；v1 不另设任意长度上限。
- 分页 `limit` 可选，范围 `1..100`，省略时为 `20`；`offset` 可选，为非负 integer，省略时为 `0`。

## 4. 冻结枚举

以下五组与 `shared/contracts/Statuses.h` 完全一致，不能增加别名：

| 状态集 | 精确值 |
| --- | --- |
| user | `active`, `frozen` |
| charger | `idle`, `reserved`, `charging`, `fault`, `restarting` |
| order | `reserved`, `charging`, `completed`, `cancelled` |
| congestion | `low`, `medium`, `high` |
| forecast run | `active`, `superseded` |

其他闭集：`system.health.status = starting|degraded|ready`，charger `type = fast|slow`，`simulator.status.state = running|paused`。

充电状态机：

```text
charger: idle -> reserved -> charging -> idle
         reserved -> idle                    (cancel)
         idle|reserved|charging -> fault -> restarting -> idle
order:   reserved -> charging -> completed
         reserved -> cancelled
```

`charge.stop` 是 charging 阶段内的停止计费点：写入 `endedAt`，但 order 仍为 `charging`、charger 仍为 `charging`，等待 `charge.settle`。

## 5. 响应 code

| code | `ok` | 精确用途 |
| --- | --- | --- |
| `OK` | true | 成功 |
| `INVALID_REQUEST` | false | JSON、action、字段类型/必填性/范围、未知 ID 或确认文本无效 |
| `UNSUPPORTED_VERSION` | false | request `version` 不是数值 `1` |
| `AUTH_REQUIRED` | false | 需要认证但 token 缺失、为空、无效或过期 |
| `FORBIDDEN` | false | 有效身份无 action 权限，或用户尝试操作非本人订单 |
| `INVALID_PHONE` | false | 手机号不匹配冻结正则 |
| `INVALID_CREDENTIALS` | false | 管理员账号或密码不正确 |
| `USER_FROZEN` | false | 冻结用户尝试预约、充值或开始充电 |
| `ACTIVE_ORDER_EXISTS` | false | 用户已有 `reserved` 或 `charging` 订单 |
| `CHARGER_NOT_AVAILABLE` | false | 桩不存在、非 `idle` 或不能进入预约 |
| `ORDER_STATE_CONFLICT` | false | 订单/桩当前权威状态不允许该转换 |
| `INSUFFICIENT_BALANCE` | false | stopped charging order 结算余额不足 |
| `MAP_API_ERROR` | false | 用户端腾讯地图地址解析/路线 API 失败；不改变服务端站点数据 |
| `FORECAST_INVALID` | false | 批次元数据、144 条记录、物理边界、唯一性或同 runId 哈希校验失败 |
| `FORECAST_STALE` | false | 仅供明确要求新鲜预测的校验场景；`forecast.latest` 不返回此错误 |
| `SERVER_BUSY` | false | 服务端连接/命令容量暂时耗尽 |
| `DB_BUSY` | false | SQLite busy 或锁等待超时 |
| `INTERNAL_ERROR` | false | 其他未分类服务端错误；不得泄露凭据或 SQL 细节 |

一般失败 `INVALID_REQUEST`、`AUTH_REQUIRED`、`FORBIDDEN`、`SERVER_BUSY`、`DB_BUSY`、`INTERNAL_ERROR` 在相关 action 均可适用；第 7 节另列主要 action-specific 失败。鉴权必须先于业务 SQL。未知 action 对所有身份返回 `INVALID_REQUEST`，权限函数则必须返回 deny。

## 6. 27 个 action 与权限矩阵

`system.health` 对匿名、已知身份和未知非空身份都允许。除它之外只有下表的 allow 单元允许；空 actor 表示 anonymous。

| action | anonymous | user | admin | simulator | ml | unknown actor |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| `auth.user_login` | allow | deny | deny | deny | deny | deny |
| `user.get` | deny | allow | deny | deny | deny | deny |
| `user.update` | deny | allow | deny | deny | deny | deny |
| `wallet.recharge` | deny | allow | deny | deny | deny | deny |
| `station.list` | deny | allow | allow | deny | deny | deny |
| `station.detail` | deny | allow | allow | deny | deny | deny |
| `charger.list` | deny | allow | allow | deny | deny | deny |
| `charge.reserve` | deny | allow | deny | deny | deny | deny |
| `charge.start` | deny | allow | deny | deny | deny | deny |
| `charge.stop` | deny | allow | deny | deny | deny | deny |
| `charge.settle` | deny | allow | deny | deny | deny | deny |
| `order.current` | deny | allow | deny | deny | deny | deny |
| `order.list` | deny | allow | deny | deny | deny | deny |
| `order.cancel` | deny | allow | deny | deny | deny | deny |
| `admin.login` | allow | deny | deny | deny | deny | deny |
| `admin.dashboard` | deny | deny | allow | deny | deny | deny |
| `admin.station_create` | deny | deny | allow | deny | deny | deny |
| `admin.charger_restart` | deny | deny | allow | deny | deny | deny |
| `admin.user_list` | deny | deny | allow | deny | deny | deny |
| `admin.user_set_status` | deny | deny | allow | deny | deny | deny |
| `telemetry.push` | deny | deny | deny | allow | deny | deny |
| `simulator.fault_set` | deny | deny | deny | allow | deny | deny |
| `simulator.status` | deny | deny | deny | allow | deny | deny |
| `forecast.publish` | deny | deny | deny | deny | allow | deny |
| `forecast.latest` | deny | allow | allow | deny | deny | deny |
| `system.health` | allow | allow | allow | allow | allow | allow |
| `demo.reset` | deny | deny | allow | deny | deny | deny |

未知或空 action 对所有 actor 均 deny。

## 7. 共享成功 schema 与逐 action 合同

### 7.1 共享成功 object

下表中的字段除标明“可省略”或 `null` 外全部必填；成功 object 不得携带 password、password hash、token（登录 action 顶层 token 除外）、客户端金额、ML metrics 或未声明字段。

#### `User`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `userId` | integer | 否 | 正整数 |
| `mobile` | string | 否 | 手机号正则 |
| `nickname` | string | 否 | trim 后非空 |
| `avatarPath` | string | 否 | 可为空字符串，表示未设置头像 |
| `balanceFen` | integer | 否 | 非负 |
| `status` | string | 否 | `active|frozen` |
| `registeredAt` | Timestamp | 否 | 注册时间 |

#### `Admin`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `adminId` | integer | 否 | 正整数 |
| `username` | string | 否 | trim 后非空 |
| `createdAt` | Timestamp | 否 | 创建时间 |

#### `Station`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `stationId` | integer | 否 | 正整数 |
| `name` | string | 否 | trim 后非空 |
| `address` | string | 否 | trim 后非空 |
| `latitude` | number | 否 | `[-90,90]` |
| `longitude` | number | 否 | `[-180,180]` |
| `priceFenPerKwh` | integer | 否 | 正整数 |
| `forecastEnabled` | boolean | 否 | 新建站固定为 `false` |
| `chargerCount` | integer | 否 | 非负 |
| `idleCount` | integer | 否 | `0..chargerCount` |
| `distanceKm` | number | 否 | 可省略；仅 `station.list` 请求同时给出坐标时出现，非负有限数 |

#### `Charger`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `chargerId` | integer | 否 | 正整数 |
| `stationId` | integer | 否 | 正整数 |
| `code` | string | 否 | trim 后非空且全局唯一 |
| `type` | string | 否 | `fast|slow` |
| `powerKw` | number | 否 | 非负有限数 |
| `status` | string | 否 | charger 冻结状态 |
| `chargeCount` | integer | 否 | 非负 |
| `totalDurationSec` | integer | 否 | 非负 |
| `updatedAt` | Timestamp | 否 | 最近权威更新时间 |

#### `Order`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `orderId` | integer | 否 | 正整数 |
| `userId` | integer | 否 | 正整数，必须是 token 用户（管理员内部视图除外） |
| `chargerId` | integer | 否 | 正整数 |
| `stationId` | integer | 否 | 正整数 |
| `stationName` | string | 否 | trim 后非空 |
| `chargerCode` | string | 否 | trim 后非空 |
| `status` | string | 否 | order 冻结状态 |
| `reservedAt` | Timestamp | 否 | 预约时间 |
| `startedAt` | Timestamp or null | 是 | `reserved/cancelled` 为 null；`charging/completed` 为 Timestamp |
| `endedAt` | Timestamp or null | 是 | 未停止的 `reserved/charging` 为 null；停止待结算、`completed/cancelled` 为 Timestamp |
| `energyKwh` | number | 否 | 非负有限数 |
| `amountFen` | integer | 否 | 非负、服务端计算 |
| `elapsedSec` | integer | 否 | 非负、服务端计算 |

#### `ForecastRun`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `runId` | string | 否 | trim 后非空 |
| `generatedAt` | Timestamp | 否 | ML 生成时间 |
| `dataCutoff` | Timestamp | 否 | `dataCutoff <= generatedAt` |
| `activatedAt` | Timestamp | 否 | 服务端接纳为 active 的时间 |
| `modelVersion` | string | 否 | trim 后非空 |
| `payloadHash` | string | 否 | 64 个小写十六进制字符的 SHA-256 |
| `stale` | boolean | 否 | `activatedAt` 距服务端当前时间超过 2 小时为 true |

#### `ForecastRecord`

| 字段 | JSON 类型 | null | 规则 |
| --- | --- | :---: | --- |
| `stationId` | integer | 否 | 正整数、属于六个 `forecastEnabled=true` 站点 |
| `forecastAt` | Timestamp | 否 | 对应 `dataCutoff + horizonH` 小时 |
| `horizonH` | integer | 否 | `1..24` |
| `predictedLoadKw` | number | 否 | `0..`该站全部桩额定功率之和，有限数 |
| `predictedBusyCount` | integer | 否 | `0..`站点 `chargerCount` |
| `predictedIdleCount` | integer | 否 | 非负，且与 busy 之和等于站点 `chargerCount` |
| `congestionLevel` | string | 否 | busy 比例 `<50%` 为 `low`，`50%..<80%` 为 `medium`，`>=80%` 为 `high` |
| `isPeak` | boolean | 否 | 是否属于该站预测峰值窗口 |

`forecast.publish.records` 对六个预测站点分别包含 `horizonH=1..24` 各一次，共 144 条，`(stationId,horizonH)` 不重复。`forecast.latest.records` 使用同一 schema 和顺序（`stationId` 升序，再按 `horizonH` 升序）。

#### 管理看板子对象

| schema | 精确字段与规则 |
| --- | --- |
| `DashboardRevenue` | `{todayRevenueFen:integer,monthRevenueFen:integer,totalRevenueFen:integer}`，三者均非负 |
| `DashboardStatusCounts` | `{idle:integer,reserved:integer,charging:integer,fault:integer,restarting:integer,total:integer}`，均非负且前五项之和等于 `total` |
| `DashboardTrendPoint` | `{date:Date,revenueFen:integer}`，金额非负 |
| `DashboardAlert` | `{stationId:integer,stationName:string,forecastAt:Timestamp,congestionLevel:string,predictedLoadKw:number,predictedBusyCount:integer,predictedIdleCount:integer,isPeak:boolean}`；congestion 和数值约束同 `ForecastRecord` |

### 7.2 逐 action 合同

每个 payload 表中的字段都是必填，除非字段名标为“可选”。“读取”表示不改变权威业务状态。

#### 1. `system.health`

- actor：任意 actor，包括匿名和未知非空 actor。
- payload：恰为 `{}`，无字段。
- success data：`{status:string,schemaVersion:integer,snapshotVersion:integer,forecastRunId:string|null,serverTime:Timestamp}`。`status=starting|degraded|ready`，`schemaVersion=1`；`snapshotVersion` 非负（仅首次快照前可为 `0`，否则为正整数）；无 active forecast 时 `forecastRunId=null`。
- 状态/转换：读取缓存健康状态，不执行 SQL。
- Qt owner：`HealthService`。
- 主要失败：`SERVER_BUSY`, `INTERNAL_ERROR`；无预测是 `degraded + ok=true`，不是错误。

#### 2. `auth.user_login`

- actor：anonymous。
- payload：`mobile` — string，必填，匹配手机号正则。
- success data：`{token:string,user:User}`，token 为 trim 后非空不透明字符串。
- 状态/转换：已有手机号返回该用户；未见过的有效手机号在事务中创建 `active` 用户后返回。冻结用户允许登录。
- Qt owner：`AuthService`。
- 主要失败：`INVALID_PHONE`, `DB_BUSY`。

#### 3. `user.get`

- actor：user。
- payload：`{}`。
- success data：`{user:User}`。
- 状态/转换：读取 token 用户；`active|frozen` 均允许。
- Qt owner：`UserService`。
- 主要失败：`AUTH_REQUIRED`。

#### 4. `user.update`

- actor：user。
- payload：`nickname` — string，必填，trim 后非空，无额外长度上限。
- success data：`{user:User}`（提交后的完整用户）。
- 状态/转换：`active|frozen` 用户均可仅更新本人 nickname；其他字段不变。
- Qt owner：`UserService`。
- 主要失败：`INVALID_REQUEST`, `DB_BUSY`。

#### 5. `wallet.recharge`

- actor：user。
- payload：`amountFen` — integer，必填，正 safe integer。
- success data：`{userId:integer,balanceFen:integer}`，userId 为 token 用户，balanceFen 为提交后的非负余额。
- 状态/转换：仅 `active` 用户；在事务中将余额增加 `amountFen`，同 requestId 重放不得再次增加。
- Qt owner：`UserService`。
- 主要失败：`USER_FROZEN`, `INVALID_REQUEST`, `DB_BUSY`。

#### 6. `station.list`

- actor：user 或 admin。
- payload：`latitude` — number，可选，`[-90,90]`；`longitude` — number，可选，`[-180,180]`。两者必须同时出现或同时省略。
- success data：`{stations:array<Station>}`。给出坐标时每项必须含 `distanceKm` 并按其升序；省略坐标时每项必须省略 `distanceKm`。
- 状态/转换：读取全部站点和动态桩计数。
- Qt owner：`StationService`。
- 主要失败：`INVALID_REQUEST`（坐标缺一或越界）。

#### 7. `station.detail`

- actor：user 或 admin。
- payload：`stationId` — integer，必填，正 safe integer。
- success data：`{station:Station,chargers:array<Charger>}`；station 省略 `distanceKm`，chargers 仅属于该站。
- 状态/转换：读取。
- Qt owner：`StationService`。
- 主要失败：`INVALID_REQUEST`（ID 无效或站点不存在）。

#### 8. `charger.list`

- actor：user 或 admin。
- payload：`stationId` — integer，必填，正 safe integer。
- success data：`{chargers:array<Charger>}`，仅返回该站充电桩。
- 状态/转换：读取。
- Qt owner：`StationService`。
- 主要失败：`INVALID_REQUEST`（ID 无效或站点不存在）。

#### 9. `charge.reserve`

- actor：user。
- payload：`chargerId` — integer，必填，正 safe integer。
- success data：`{order:Order}`，新 order 为 `reserved`。
- 状态/转换：要求用户 `active`、没有 `reserved|charging` order、charger 为 `idle`；一个事务完成 charger `idle->reserved` 和新 order `reserved`。
- Qt owner：`ChargeService`。
- 主要失败：`USER_FROZEN`, `ACTIVE_ORDER_EXISTS`, `CHARGER_NOT_AVAILABLE`。

#### 10. `charge.start`

- actor：user。
- payload：`orderId` — integer，必填，正 safe integer。
- success data：`{order:Order}`，返回 `charging` 且 `startedAt` 非 null。
- 状态/转换：要求 `active` 用户拥有该 `reserved` order，关联 charger 为 `reserved`；同一事务 order `reserved->charging`、charger `reserved->charging`。
- Qt owner：`ChargeService`。
- 主要失败：`USER_FROZEN`, `FORBIDDEN`, `ORDER_STATE_CONFLICT`。

#### 11. `charge.stop`

- actor：user。
- payload：`orderId` — integer，必填，正 safe integer。
- success data：`{order:Order}`，status 仍为 `charging`、`endedAt` 为停止 Timestamp。
- 状态/转换：要求调用者拥有 `charging` order 且 `endedAt=null`；写入 `endedAt`，order/charger status 均保持 `charging` 等待结算。冻结用户仍可停止。
- Qt owner：`ChargeService`。
- 主要失败：`FORBIDDEN`, `ORDER_STATE_CONFLICT`。

#### 12. `charge.settle`

- actor：user。
- payload：`orderId` — integer，必填，正 safe integer。
- success data：`{order:Order,balanceFen:integer}`，order 为 `completed`，余额非负。
- 状态/转换：要求调用者拥有 status=`charging` 且 `endedAt!=null` 的 order，并有足够余额；同一事务扣款、order `charging->completed`、charger `charging->idle`。冻结用户仍可结算。
- Qt owner：`ChargeService`。
- 主要失败：`FORBIDDEN`, `ORDER_STATE_CONFLICT`, `INSUFFICIENT_BALANCE`。

#### 13. `order.current`

- actor：user。
- payload：`{}`。
- success data：`{order:Order|null}`；仅 token 用户的唯一 `reserved|charging` order，无活动订单时恰为 null。
- 状态/转换：读取；`active|frozen` 均允许。
- Qt owner：`ChargeService`。
- 主要失败：`AUTH_REQUIRED`。

#### 14. `order.list`

- actor：user。
- payload：`limit` — integer，可选，`1..100`，默认 `20`；`offset` — integer，可选，非负，默认 `0`。
- success data：`{items:array<Order>,total:integer}`，仅 token 用户订单，total 为分页前非负总数；items 按 `reservedAt` 降序。
- 状态/转换：读取；`active|frozen` 均允许。
- Qt owner：`ChargeService`。
- 主要失败：`INVALID_REQUEST`（分页越界）。

#### 15. `order.cancel`

- actor：user。
- payload：`orderId` — integer，必填，正 safe integer。
- success data：`{order:Order}`，order 为 `cancelled`。
- 状态/转换：要求调用者拥有 `reserved` order 且 charger 为 `reserved`；同一事务 order `reserved->cancelled`、charger `reserved->idle`。冻结用户仍可取消。
- Qt owner：`ChargeService`。
- 主要失败：`FORBIDDEN`, `ORDER_STATE_CONFLICT`。

#### 16. `admin.login`

- actor：anonymous。
- payload：`username` — string，必填，trim 后非空；`password` — string，必填，trim 后非空。
- success data：`{token:string,admin:Admin}`，token 为 trim 后非空不透明字符串，响应不得含密码或哈希。
- 状态/转换：只读验证凭据并创建进程内会话 token。
- Qt owner：`AuthService`。
- 主要失败：`INVALID_CREDENTIALS`, `INVALID_REQUEST`。

#### 17. `admin.dashboard`

- actor：admin。
- payload：`rangeDays` — integer，必填，恰为 `7` 或 `30`。
- success data：`{revenue:DashboardRevenue,statusCounts:DashboardStatusCounts,trend:array<DashboardTrendPoint>,alerts:array<DashboardAlert>}`；trend 恰有 rangeDays 个连续 Date，缺失日期 revenueFen 为 `0`；alerts 是 active forecast 中 `congestionLevel=high` 或 `isPeak=true` 的记录，按 forecastAt、stationId 升序。
- 状态/转换：读取当前统计和 active forecast。
- Qt owner：`AdminService`。
- 主要失败：`INVALID_REQUEST`（rangeDays 非 7/30）。

#### 18. `admin.station_create`

- actor：admin。
- payload：`name`、`address` — string，必填，trim 后非空；`latitude` — number，必填，`[-90,90]`；`longitude` — number，必填，`[-180,180]`；`priceFenPerKwh` — integer，必填，正 safe integer；`fastChargerCount`、`slowChargerCount` — integer，必填，非负 safe integer，且两者之和至少为 `1`。
- success data：`{station:Station,chargers:array<Charger>}`；station 的 `forecastEnabled=false` 且省略 distanceKm；chargers 数量等于两类计数之和，全部为 `idle`，type 分别为 `fast|slow`。
- 状态/转换：一个事务创建站点与全部桩；不会改变固定六站/144 条预测范围。
- Qt owner：`AdminService`。
- 主要失败：`INVALID_REQUEST`, `DB_BUSY`。

#### 19. `admin.charger_restart`

- actor：admin。
- payload：`chargerId` — integer，必填，正 safe integer。
- success data：`{charger:Charger}`，立即返回的 charger 为 `restarting`。
- 状态/转换：仅权威状态 `fault` 可开始；DB worker 事务执行 `fault->restarting`，随后由 DB worker 的确定性任务执行 `restarting->idle`。任何 UI/timer 线程不得直接写 SQLite。
- Qt owner：`AdminService`。
- 主要失败：`ORDER_STATE_CONFLICT`（不是 fault）, `INVALID_REQUEST`（ID 无效/不存在）。

#### 20. `admin.user_list`

- actor：admin。
- payload：`mobileLike` — string，可选，仅含 `0..11` 个十进制数字；空字符串或省略表示无手机号过滤。`limit` — integer，可选，`1..100` 默认 `20`；`offset` — integer，可选，非负默认 `0`。
- success data：`{items:array<User>,total:integer}`，total 为过滤后、分页前非负总数。
- 状态/转换：读取。
- Qt owner：`AdminService`。
- 主要失败：`INVALID_REQUEST`（过滤串或分页无效）。

#### 21. `admin.user_set_status`

- actor：admin。
- payload：`userId` — integer，必填，正 safe integer；`status` — string，必填，恰为 `active|frozen`。
- success data：`{user:User}`，为提交后的完整用户。
- 状态/转换：`active|frozen -> active|frozen`，同值设置为幂等成功。冻结不会破坏已有订单；用户仍能 stop/cancel/settle 自己的已有订单。
- Qt owner：`AdminService`。
- 主要失败：`INVALID_REQUEST`（ID/状态无效或用户不存在）, `DB_BUSY`。

#### 22. `telemetry.push`

- actor：simulator。
- payload：`chargerId` — integer，必填，正 safe integer；`recordedAt` — Timestamp，必填，必须严格晚于该 charger 最后接纳样本；`powerKw`、`energyIncrementKwh` — number，必填，非负有限数；`status` — string，必填，charger 冻结状态且必须与权威状态一致。字段名必须是 `recordedAt`，不接受旧拼写 `observedAt` 作为权威时间。
- success data：`{acceptedAt:Timestamp,order:Order|null}`。存在关联 charging order 时返回该 order，否则为 null。
- 状态/转换：记录样本；仅当权威 charger=`charging` 且 order=`charging,endedAt=null` 时累加 energy 并重算服务端 amount。stop 后样本不得增加 energy/amount。payload status 不驱动状态转换。
- Qt owner：`TelemetryService`。
- 主要失败：`INVALID_REQUEST`（未知 charger、时间不递增、非有限/负数、状态不一致）, `ORDER_STATE_CONFLICT`。

#### 23. `simulator.fault_set`

- actor：simulator。
- payload：`chargerId` — integer，必填，正 safe integer；`fault` — boolean，必填；`recordedAt` — Timestamp，必填。
- success data：`{charger:Charger}`。
- 状态/转换：`fault=true` 要求当前为 `idle|reserved|charging` 并转为 `fault`；`fault=false` 仅在当前为 `fault` 时记录恢复意图，charger 保持 `fault`，不得绕过管理员的 `fault->restarting->idle`。活动 order 不由该 action 静默完成、取消或结算。
- Qt owner：`TelemetryService`。
- 主要失败：`INVALID_REQUEST`（charger/时间无效）, `ORDER_STATE_CONFLICT`（布尔意图与当前状态不匹配）。

#### 24. `simulator.status`

- actor：simulator。
- payload：`state` — string，必填，`running|paused`；`simulatedAt` — Timestamp，必填；`eventCount` — integer，必填，非负 safe integer。
- success data：`{acceptedAt:Timestamp,chargers:array<Charger>}`，chargers 是用于重连同步的完整权威快照。
- 状态/转换：记录模拟器心跳/游标，不以 payload 覆盖 charger 状态。
- Qt owner：`SimulatorService`。
- 主要失败：`INVALID_REQUEST`（state、时间或计数无效）。

#### 25. `forecast.publish`

- actor：ml。
- payload：`runId`、`modelVersion` — string，必填，trim 后非空；`generatedAt`、`dataCutoff` — Timestamp，必填且 `dataCutoff<=generatedAt`；`records` — `array<ForecastRecord>`，必填，必须满足第 7.1 节全部边界、六站 × 24 horizon、唯一性与 144 条规则。payload 不接受 metrics。
- success data：`{runId:string,acceptedCount:integer,snapshotReady:boolean}`；runId 与请求一致，acceptedCount 恰为 `144`；snapshotReady 表示提交后对应 Web 快照是否已经原子发布，false 时 SQLite 仍是权威来源。
- 状态/转换：先在内存完整校验，再在 DB worker 单事务执行：旧 `active->superseded`，插入新 active run（`activatedAt` 由服务端生成）、插入 144 条、保存幂等响应、递增 snapshotVersion、提交。任一步失败整批回滚，旧 active 保持不变。同 runId+同 payloadHash 重放原 ACK；同 runId+不同 hash 失败。
- Qt owner：`ForecastService`。
- 主要失败：`FORECAST_INVALID`, `DB_BUSY`。

#### 26. `forecast.latest`

- actor：user 或 admin。
- payload：`{}`。
- success data：`{forecastRun:ForecastRun|null,records:array<ForecastRecord>}`。没有 active run 时必须恰为 `{forecastRun:null,records:[]}`；有 active run 时 records 恰为 144 条。
- 状态/转换：读取。即使超过两小时也返回 `ok=true`、完整记录和 `forecastRun.stale=true`，不得返回 `FORECAST_STALE` 阻止查看。
- Qt owner：`ForecastService`。
- 主要失败：仅一般读取失败；stale 不是失败。

#### 27. `demo.reset`

- actor：admin。
- payload：`confirmation` — string，必填，必须逐字等于 `RESET_DEMO`。
- success data：`{resetAt:Timestamp,goldenHash:string}`；goldenHash 为已批准黄金库的 64 位小写 SHA-256。
- 状态/转换：运行中 reset 只由 `DemoResetService` 排队到 DB worker，在显式表清单的单一事务中恢复批准数据、保存本次幂等响应并重建快照；失败保持运行库和旧快照不变。文件级数据库替换只能在服务端停止时执行，不是此 action 的行为。
- Qt owner：`DemoResetService`。
- 主要失败：`INVALID_REQUEST`（确认文本不符）, `INTERNAL_ERROR`（黄金哈希/恢复验证失败）, `DB_BUSY`。

## 8. 事务、SQLite 与 dashboard snapshot 所有权

- Qt 管理/服务端是运行期 SQLite 的唯一 writer。所有业务 mutation 和 `request_log` 幂等记录都在其串行 `DatabaseWorker` 上执行，并使用事务；UI、socket worker、timer 不直接写 SQLite。
- 模拟器与 ML 仅通过本文 TCP action 发布；用户端从不访问 SQLite；Web 只读 JSON，不访问 SQLite。
- `SnapshotWriter` 只存在于 Qt 管理/服务端。它在已提交 mutation 后独占创建完整 UTF-8 JSON 临时文件并原子替换 `dashboard/runtime/dashboard_snapshot.json`；文件失败不回滚已提交 DB，而是保留 last-known-good 文件并重试对应版本。
- Web 只轮询该路径。获取、解析或 schema 校验失败时必须保留最后一次有效数据并显示 stale/error，不得用空白或部分新数据覆盖。
- 运行期 `demo.reset` 由 DB worker 执行；停止服务后的文件替换工具必须先证明服务端未运行。两种路径不得并行。

## 9. 冻结治理、签字与 tag

冻结后只允许向后兼容、可忽略的新增字段。现有字段、27 个 action、五组 status、身份权限、枚举值和语义不得改名、删除、重新解释或缩窄原有合法范围；任何兼容新增都必须同时更新本文和可执行合同测试。

不得伪造签名。当前确认状态如下：

| 审核人 | 角色 | 状态 | 完成确认所需证据 |
| --- | --- | --- | --- |
| #2 杨佳车 | TL | 待确认 | 完成 PR review，确认 Qt 服务 owner、事务/状态转换、payload 与 success schema 可实现且一致 |
| #3 胡晟源 | PRL | 待确认 | 完成最终集成验证，确认用户端消费字段、权限、错误与 stale/null 行为一致 |

`v0.1-contract` 只能由 #4 SCML 在 PR 已合并且以上两位实际人员都明确确认后创建。本次合同补全不得自行创建、移动或伪造该 tag。
