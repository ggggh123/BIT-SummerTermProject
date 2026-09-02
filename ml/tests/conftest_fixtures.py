"""Test fixtures: small CSV history generator."""

from __future__ import annotations

import math
import random
from pathlib import Path

import pandas as pd


def generate_fixture_csv(path: str | Path, n_stations: int = 6, n_hours: int = 24 * 90) -> None:
    """Generate a small deterministic fixture CSV matching the real schema.

    Uses a fixed seed and a simple sinusoidal pattern so that
    seasonal-naive (yesterday same hour) is a strong baseline.
    """
    rng = random.Random(42)
    lines = ["station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw"]
    start = pd.Timestamp("2026-06-03T09:00:00+08:00")
    for sid in range(1, n_stations + 1):
        for h in range(n_hours):
            ts = start + pd.Timedelta(hours=h)
            hour = ts.hour
            dow = ts.dayofweek
            # Daily pattern: peak around midday, trough at night
            daily_factor = 0.5 + 0.5 * math.sin(2 * math.pi * (hour - 6) / 24)
            station_offset = (sid - 1) * 20
            base_load = (100 + station_offset) * daily_factor + rng.uniform(-10, 10)
            base_busy = int(4 * daily_factor + rng.uniform(-0.5, 0.5))
            base_busy = max(0, min(8, base_busy))
            temp = 15 + 10 * math.sin(2 * math.pi * (hour - 6) / 24) + rng.uniform(-2, 2)
            is_holiday = 1 if dow >= 5 else 0
            load = round(max(0, min(360, base_load)), 3)
            lines.append(
                f"{sid},{ts.strftime('%Y-%m-%dT%H:%M:%S+08:00')},8,360.0,{round(temp,2)},{is_holiday},{base_busy},{load}"
            )

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
