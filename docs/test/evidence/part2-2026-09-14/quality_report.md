# 数据质量探查报告

| 规则 | 类型 | 注入 | 检出 | 召回 |
|---|---|---|---|---|
| R01 | 缺失值 | 494 | 481 | 0.97 |
| R02 | 重复记录 | 858 | 2084 | 1.0 |
| R03 | 异常值 | 675 | 680 | 1.0 |
| R04 | 时间格式混杂 | 603 | 607 | 1.0 |
| R05 | 逻辑矛盾 | 98 | 99 | 1.0 |
| R06 | 金额口径错误 | 94 | 147 | 1.0 |
| R07 | 孤儿引用 | 60 | 60 | 1.0 |
| R08 | 非法字段值 | 12 | 12 | 1.0 |
| R09 | 经纬度越界 | 1 | 1 | 1.0 |
| R10 | 文本脏数据 | 8 | 8 | 1.0 |

## 各表画像

| 表 | 行数 | 空值率（前 3 列） |
|---|---|---|
| stations | 8 | id=0.0, name=0.0, address=0.0 |
| chargers | 30 | id=0.0, station_id=0.0, code=0.0 |
| users | 500 | avatar_path=1.0, nickname=0.018, id=0.0 |
| orders | 12196 | ended_at=0.0046, id=0.0, user_id=0.0 |
| telemetry | 100662 | power_kw=0.0043, id=0.0, charger_id=0.0 |
| station_hourly_history | 17280 | station_id=0.0, observed_at=0.0, pile_count=0.0 |
| events | 2000 | id=0.0, event_type=0.0, entity_type=0.0 |
