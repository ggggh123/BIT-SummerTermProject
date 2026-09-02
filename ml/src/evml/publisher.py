"""Publish forecast run to Qt server via v1 TCP protocol."""

from __future__ import annotations

import json
import os
import socket
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .protocol import build_request_envelope, encode_frame, recv_frame, parse_response_envelope
from .types import ForecastRun


@dataclass(frozen=True)
class PublishReceipt:
    """Result of a forecast.publish attempt."""

    run_id: str
    accepted_count: int
    snapshot_ready: bool
    ok: bool
    code: str
    message: str


def publish_forecast(
    host: str,
    port: int,
    token: str,
    run: ForecastRun,
    timeout_s: float = 3.0,
) -> PublishReceipt:
    """Publish a forecast run to the Qt server.

    The request ID is deterministic: ``publish-<runId>`` so retries
    are idempotent.

    Parameters
    ----------
    host
        Server host.
    port
        Server port.
    token
        ML service token.
    run
        ForecastRun to publish.
    timeout_s
        Socket timeout in seconds.

    Returns
    -------
    PublishReceipt with server response.
    """
    request_id = f"publish-{run.run_id}"
    payload = {
        "runId": run.run_id,
        "generatedAt": run.generated_at,
        "dataCutoff": run.data_cutoff,
        "modelVersion": run.model_version,
        "records": [
            {
                "stationId": r.station_id,
                "forecastAt": r.forecast_at,
                "horizonH": r.horizon_h,
                "predictedLoadKw": r.predicted_load_kw,
                "predictedBusyCount": r.predicted_busy_count,
                "predictedIdleCount": r.predicted_idle_count,
                "congestionLevel": r.congestion_level,
                "isPeak": r.is_peak,
            }
            for r in run.records
        ],
    }

    envelope = build_request_envelope(
        request_id=request_id,
        action="forecast.publish",
        payload=payload,
        token=token,
    )
    frame = encode_frame(envelope)

    sock = socket.create_connection((host, port), timeout=timeout_s)
    try:
        sock.sendall(frame)
        response_raw = recv_frame(sock, timeout_s)
    finally:
        sock.close()

    resp = parse_response_envelope(response_raw)
    data = resp.get("data", {})
    return PublishReceipt(
        run_id=run.run_id,
        accepted_count=int(data.get("acceptedCount", 0)),
        snapshot_ready=bool(data.get("snapshotReady", False)),
        ok=bool(resp["ok"]),
        code=resp["code"],
        message=resp["message"],
    )


def save_last_good(path: str | Path, publish_payload: dict[str, Any]) -> None:
    """Atomically write the last-good forecast payload.

    The file is a replayable publish payload (the same dict that was
    sent as the ``forecast.publish`` payload).  The write is atomic:
    a temp file is written, fsync'd, then os.replace'd to the target.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(publish_payload, f, ensure_ascii=False, sort_keys=True, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
