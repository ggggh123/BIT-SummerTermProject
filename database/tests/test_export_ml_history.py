import csv

from export_ml_history import export_history
from helpers import build_temp_db

HEADER = ["station_id", "observed_at", "pile_count", "rated_power_kw",
          "temperature_c", "is_holiday", "busy_count", "load_kw"]


def test_export_history_writes_12960_sorted_rows(tmp_path):
    build_temp_db(tmp_path)  # creates demo.db
    out = tmp_path / "ml" / "station_hourly_history.csv"
    n = export_history(tmp_path / "demo.db", out)
    assert n == 12960

    with open(out, newline="", encoding="utf-8") as fh:
        rows = list(csv.reader(fh))

    assert rows[0] == HEADER
    data = rows[1:]
    assert len(data) == 12960
    assert {r[0] for r in data} == {str(i) for i in range(1, 7)}
    # strictly ordered by (station_id, observed_at)
    keys = [(int(r[0]), r[1]) for r in data]
    assert keys == sorted(keys)
