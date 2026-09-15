"""前端字段结构对齐校验 —— 验证「关掉 mock 切真接口，组件代码不用动」。

    python server/tools/selftest_frontend_shape.py

做法：把 `web/tests/fixtures/*.json`（前端契约夹具，字段真源）逐个与真实接口响应做
**结构性**比对：

  * mock 里出现的每个字段，接口必须都有且类型兼容 → 缺一个前端就读到 `undefined`
  * 接口多出的字段只记录、不判失败（前端不使用，无害）
  * 数值大小、数组长度不做判定（真实数据规模与 mock 必然不同）

比的是「契约形状」，不是「契约取值」，因此可持续用于回归。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app import app  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
MOCK_DIR = REPO_ROOT / "web" / "tests" / "fixtures"

# (mock 文件, 接口路径, 查询参数)
CASES: list[tuple[str, str, dict]] = [
    ("overview_kpis.json", "/api/overview/kpis", {}),
    ("overview_stations.json", "/api/overview/stations", {}),
    ("overview_charger-status.json", "/api/overview/charger-status", {}),
    ("overview_load-24h.json", "/api/overview/load-24h", {}),
    ("overview_events.json", "/api/overview/events", {}),
    ("enterprise_revenue-trend.json", "/api/enterprise/revenue-trend", {"days": 30}),
    ("enterprise_station-ranking.json", "/api/enterprise/station-ranking", {}),
    ("enterprise_user-growth.json", "/api/enterprise/user-growth", {"days": 30}),
    ("enterprise_user-rfm.json", "/api/enterprise/user-rfm", {}),
    ("enterprise_monthly.json", "/api/enterprise/monthly", {}),
    ("quality_summary.json", "/api/quality/summary", {}),
    ("user_price-compare.json", "/api/user/price-compare", {}),
    ("user_price-distance.json", "/api/user/price-distance", {}),
    ("user_idle-ranking.json", "/api/user/idle-ranking", {}),
    ("user_peak-heatmap.json", "/api/user/peak-heatmap", {}),
    ("station_coverage.json", "/api/station/coverage", {}),
    ("gov_coverage.json", "/api/gov/coverage", {}),
    ("gov_service-stats.json", "/api/gov/service-stats", {}),
    ("gov_carbon.json", "/api/gov/carbon", {}),
    ("gov_peak-load.json", "/api/gov/peak-load", {}),
    ("gov_utilization.json", "/api/gov/utilization", {}),
    ("forecast_24h.json", "/api/forecast/24h", {}),
]

# station_detail.json 一个 mock 文件覆盖三条端点，单独处理
STATION_DETAIL_CASES = [
    ("utilization", "/api/station/{sid}/utilization", {}),
    ("mix", "/api/station/{sid}/mix", {}),
    ("health", "/api/station/{sid}/health", {}),
]


def type_compatible(mock_value, api_value) -> bool:
    """类型兼容：bool 不与 int 混同；int 与 float 视为同族数值。"""
    if isinstance(mock_value, bool) or isinstance(api_value, bool):
        return isinstance(mock_value, bool) and isinstance(api_value, bool)
    if isinstance(mock_value, (int, float)) and isinstance(api_value, (int, float)):
        return True
    if isinstance(mock_value, str) and isinstance(api_value, str):
        return True
    return isinstance(mock_value, type(api_value))


def diff(mock_value, api_value, path: str, problems: list[str], extras: list[str]) -> None:
    if isinstance(mock_value, dict):
        if not isinstance(api_value, dict):
            problems.append(f"{path}: mock 是对象，接口是 {type(api_value).__name__}")
            return
        for key in sorted(set(mock_value) - set(api_value)):
            problems.append(f"{path}.{key}: 接口缺失（前端会读到 undefined）")
        for key in sorted(set(api_value) - set(mock_value)):
            extras.append(f"{path}.{key}")
        for key in sorted(set(mock_value) & set(api_value)):
            diff(mock_value[key], api_value[key], f"{path}.{key}", problems, extras)
        return

    if isinstance(mock_value, list):
        if not isinstance(api_value, list):
            problems.append(f"{path}: mock 是数组，接口是 {type(api_value).__name__}")
            return
        if mock_value and not api_value:
            problems.append(f"{path}: 接口返回空数组（前端图表会空白）")
            return
        if mock_value and api_value:
            diff(mock_value[0], api_value[0], f"{path}[0]", problems, extras)
        return

    if not type_compatible(mock_value, api_value):
        problems.append(
            f"{path}: 类型不兼容 mock={type(mock_value).__name__} api={type(api_value).__name__}"
        )


def load_mock(name: str) -> dict:
    with (MOCK_DIR / name).open(encoding="utf-8") as handle:
        return json.load(handle)["data"]


def main() -> int:
    if not MOCK_DIR.is_dir():
        print(f"找不到前端契约夹具目录：{MOCK_DIR}")
        return 1

    all_problems: list[str] = []
    all_extras: list[str] = []
    total = 0

    with app.test_client() as client:
        for mock_name, path, params in CASES:
            total += 1
            mock_data = load_mock(mock_name)
            body = client.get(path, query_string=params).get_json()
            if not body or body.get("code") != 0:
                all_problems.append(f"{mock_name} ↔ {path}: 接口 code={(body or {}).get('code')}")
                continue
            problems: list[str] = []
            diff(mock_data, body["data"], path, problems, all_extras)
            if problems:
                all_problems.extend(f"{mock_name} ↔ {p}" for p in problems)
            print(f"[{'PASS' if not problems else 'FAIL'}] {mock_name} ↔ {path}"
                  + (f"  ({len(problems)} 处不兼容)" if problems else ""))

        detail = load_mock("station_detail.json")
        for sid in range(1, 9):
            node = detail.get(str(sid))
            if node is None:
                all_problems.append(f"station_detail.json 缺少站点 {sid}")
                continue
            for key, template, params in STATION_DETAIL_CASES:
                total += 1
                path = template.format(sid=sid)
                body = client.get(path, query_string=params).get_json()
                if not body or body.get("code") != 0:
                    all_problems.append(f"station_detail[{sid}].{key} ↔ {path}: 接口非 0")
                    continue
                problems = []
                diff(node[key], body["data"], path, problems, all_extras)
                if problems:
                    all_problems.extend(f"station_detail[{sid}].{key} ↔ {p}" for p in problems)
                print(f"[{'PASS' if not problems else 'FAIL'}] station_detail[{sid}].{key} ↔ {path}"
                      + (f"  ({len(problems)} 处不兼容)" if problems else ""))

    print("\n" + "=" * 72)
    print(f"合计 {total} 项结构性比对，不兼容 {len(all_problems)} 处")
    if all_extras:
        unique = sorted(set(all_extras))
        print(f"\n接口新增字段 {len(unique)} 个（前端未使用，无害，仅记录）：")
        for extra in unique[:15]:
            print(f"  + {extra}")
        if len(unique) > 15:
            print(f"  ... 另有 {len(unique) - 15} 个")
    if all_problems:
        print("\n不兼容明细：")
        for problem in all_problems[:40]:
            print(f"  - {problem}")
    return 1 if all_problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
