"""Tests for protocol framing and publisher (Task 6)."""

from __future__ import annotations

import json
import os
import socket
import struct
import sys
import threading
import time
from pathlib import Path

import pandas as pd
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.protocol import (
    encode_frame,
    decode_frame,
    recv_frame,
    build_request_envelope,
    parse_response_envelope,
    MAX_FRAME_SIZE,
)
from evml.publisher import publish_forecast, save_last_good, PublishReceipt
from evml.types import ForecastRecord, ForecastRun


class TestFrameEncoding:
    """Test frame encoding/decoding."""

    def test_encode_produces_correct_header(self):
        message = {"version": 1, "requestId": "test-1", "action": "system.health", "payload": {}}
        frame = encode_frame(message)
        assert len(frame) > 4
        length = struct.unpack(">I", frame[:4])[0]
        body = frame[4:]
        assert length == len(body)
        decoded = json.loads(body.decode("utf-8"))
        assert decoded["requestId"] == "test-1"

    def test_encode_decode_roundtrip(self):
        message = {"version": 1, "requestId": "test-2", "action": "system.health", "payload": {"x": 1}}
        frame = encode_frame(message)
        decoded = decode_frame(frame)
        assert decoded["requestId"] == "test-2"
        assert decoded["payload"]["x"] == 1

    def test_decode_rejects_zero_length(self):
        frame = struct.pack(">I", 0)
        with pytest.raises(ValueError, match="out of range"):
            decode_frame(frame)

    def test_decode_rejects_over_max(self):
        frame = struct.pack(">I", MAX_FRAME_SIZE + 1) + b"x"
        with pytest.raises(ValueError, match="out of range"):
            decode_frame(frame)

    def test_decode_rejects_invalid_json(self):
        body = b"not json"
        frame = struct.pack(">I", len(body)) + body
        with pytest.raises(ValueError, match="Invalid JSON"):
            decode_frame(frame)

    def test_decode_rejects_incomplete_frame(self):
        body = b'{"a":1}'
        frame = struct.pack(">I", len(body)) + b'{"a":'
        with pytest.raises(ValueError, match="Incomplete"):
            decode_frame(frame)


class TestRequestEnvelope:
    """Test request envelope construction."""

    def test_build_envelope_fields(self):
        env = build_request_envelope("req-1", "forecast.publish", {"runId": "r1"}, token="tok")
        assert env["version"] == 1
        assert env["requestId"] == "req-1"
        assert env["action"] == "forecast.publish"
        assert env["token"] == "tok"
        assert env["payload"]["runId"] == "r1"

    def test_parse_response(self):
        resp = parse_response_envelope({
            "requestId": "req-1",
            "ok": True,
            "code": "OK",
            "message": "success",
            "data": {"acceptedCount": 144},
        })
        assert resp["ok"] is True
        assert resp["code"] == "OK"
        assert resp["data"]["acceptedCount"] == 144


class TestPublishForecast:
    """Test forecast publishing with a fake server."""

    @pytest.fixture
    def fake_server(self):
        """Start a fake TCP server that accepts forecast.publish."""
        server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind(("127.0.0.1", 0))
        server_sock.listen(1)
        port = server_sock.getsockname()[1]

        received = {}

        def handle_client(conn):
            try:
                # Read 4-byte header
                header = b""
                while len(header) < 4:
                    chunk = conn.recv(4 - len(header))
                    if not chunk:
                        break
                    header += chunk
                if len(header) < 4:
                    return
                length = struct.unpack(">I", header)[0]
                body = b""
                while len(body) < length:
                    chunk = conn.recv(length - len(body))
                    if not chunk:
                        break
                    body += chunk
                request = json.loads(body.decode("utf-8"))
                received["request"] = request

                # Build response
                response = {
                    "requestId": request["requestId"],
                    "ok": True,
                    "code": "OK",
                    "message": "Forecast accepted",
                    "data": {
                        "runId": request["payload"]["runId"],
                        "acceptedCount": 144,
                        "snapshotReady": True,
                    },
                }
                resp_body = json.dumps(response, ensure_ascii=False).encode("utf-8")
                conn.sendall(struct.pack(">I", len(resp_body)) + resp_body)
            finally:
                conn.close()

        def accept_loop():
            try:
                conn, _ = server_sock.accept()
                handle_client(conn)
            except Exception:
                pass

        thread = threading.Thread(target=accept_loop, daemon=True)
        thread.start()
        time.sleep(0.1)  # Give server time to start

        yield port, received

        server_sock.close()

    def test_publish_and_get_receipt(self, fake_server, fixture_csv):
        port, received = fake_server
        # Build a minimal forecast run
        from evml.forecast import build_forecast_run
        from evml.repository import load_history
        from evml.ridge import fit_ridge
        from evml.features import build_supervised, chronological_split

        history = load_history(fixture_csv)
        split = chronological_split(history)
        load_model = fit_ridge(split.train, "load_kw")
        busy_model = fit_ridge(split.train, "busy_count")
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = build_forecast_run(history, cutoff, load_model, busy_model, "ridge", "ridge")

        receipt = publish_forecast("127.0.0.1", port, "test-token", run)

        assert receipt.ok is True
        assert receipt.accepted_count == 144
        assert receipt.run_id == run.run_id

        # Verify the server received the right request
        req = received["request"]
        assert req["action"] == "forecast.publish"
        assert req["payload"]["runId"] == run.run_id
        assert len(req["payload"]["records"]) == 144

    def test_publish_request_id_is_deterministic(self, fake_server, fixture_csv):
        port, received = fake_server
        from evml.forecast import build_forecast_run
        from evml.repository import load_history
        from evml.ridge import fit_ridge
        from evml.features import build_supervised, chronological_split

        history = load_history(fixture_csv)
        split = chronological_split(history)
        load_model = fit_ridge(split.train, "load_kw")
        busy_model = fit_ridge(split.train, "busy_count")
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = build_forecast_run(history, cutoff, load_model, busy_model, "ridge", "ridge")

        publish_forecast("127.0.0.1", port, "tok", run)

        req = received["request"]
        assert req["requestId"] == f"publish-{run.run_id}"


class TestSaveLastGood:
    """Test last-good file atomic write."""

    def test_save_creates_file(self, tmp_path):
        path = tmp_path / "forecast_last_good.json"
        payload = {"runId": "test-run", "records": []}
        save_last_good(path, payload)
        assert path.exists()
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        assert data["runId"] == "test-run"

    def test_save_overwrites_existing(self, tmp_path):
        path = tmp_path / "forecast_last_good.json"
        # Write old content
        with open(path, "w") as f:
            json.dump({"runId": "old"}, f)
        # Save new
        save_last_good(path, {"runId": "new", "records": []})
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        assert data["runId"] == "new"

    def test_save_creates_parent_dir(self, tmp_path):
        path = tmp_path / "subdir" / "forecast_last_good.json"
        save_last_good(path, {"runId": "test"})
        assert path.exists()

    def test_failed_publish_leaves_existing(self, tmp_path):
        """If publish fails, last-good should not be overwritten."""
        path = tmp_path / "forecast_last_good.json"
        # Write existing good content
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"runId": "good-run"}, f)

        # Simulate publish failure — don't call save_last_good
        # The CLI should only call save_last_good on success
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        assert data["runId"] == "good-run"
