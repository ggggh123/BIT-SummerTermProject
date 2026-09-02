"""Python implementation of the v1 TCP frame and JSON envelope protocol."""

from __future__ import annotations

import json
import struct
from typing import Any

MAX_FRAME_SIZE = 1_048_576  # 1 MiB


def encode_frame(message: dict[str, Any]) -> bytes:
    """Encode a JSON message into a 4-byte big-endian length-prefixed frame.

    Parameters
    ----------
    message
        JSON-serializable dict.

    Returns
    -------
    bytes: ``[4-byte big-endian length][UTF-8 JSON payload]``.
    """
    body = json.dumps(message, ensure_ascii=False, sort_keys=True).encode("utf-8")
    length = len(body)
    if length == 0 or length > MAX_FRAME_SIZE:
        raise ValueError(f"Frame body length {length} out of range [1, {MAX_FRAME_SIZE}]")
    header = struct.pack(">I", length)
    return header + body


def decode_frame(data: bytes) -> dict[str, Any]:
    """Decode a single complete frame from bytes.

    Raises ValueError if data is incomplete, zero-length, over-limit,
    or contains invalid JSON.
    """
    if len(data) < 4:
        raise ValueError("Incomplete frame: less than 4 header bytes")
    length = struct.unpack(">I", data[:4])[0]
    if length == 0 or length > MAX_FRAME_SIZE:
        raise ValueError(f"Frame length {length} out of range [1, {MAX_FRAME_SIZE}]")
    if len(data) < 4 + length:
        raise ValueError("Incomplete frame: payload shorter than declared length")
    body = data[4 : 4 + length]
    try:
        return json.loads(body.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise ValueError(f"Invalid JSON in frame: {e}")


def recv_frame(sock, timeout_s: float = 3.0) -> dict[str, Any]:
    """Receive and decode one frame from a socket.

    Handles split reads (partial frame arriving in multiple recv calls).
    """
    import socket as _socket

    sock.settimeout(timeout_s)

    # Read 4-byte header
    header = _recv_exactly(sock, 4, timeout_s)
    if len(header) < 4:
        raise ValueError("Connection closed before frame header received")

    length = struct.unpack(">I", header)[0]
    if length == 0 or length > MAX_FRAME_SIZE:
        raise ValueError(f"Frame length {length} out of range [1, {MAX_FRAME_SIZE}]")

    # Read payload
    body = _recv_exactly(sock, length, timeout_s)
    if len(body) < length:
        raise ValueError("Connection closed during frame payload")

    try:
        return json.loads(body.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise ValueError(f"Invalid JSON in frame: {e}")


def _recv_exactly(sock, n: int, timeout_s: float) -> bytes:
    """Receive exactly n bytes from socket, handling partial reads."""
    data = b""
    while len(data) < n:
        try:
            chunk = sock.recv(n - len(data))
        except Exception:
            break
        if not chunk:
            break
        data += chunk
    return data


def build_request_envelope(
    request_id: str,
    action: str,
    payload: dict[str, Any],
    token: str = "",
    version: int = 1,
) -> dict[str, Any]:
    """Build a v1 RequestEnvelope."""
    return {
        "version": version,
        "requestId": request_id,
        "action": action,
        "token": token,
        "payload": payload,
    }


def parse_response_envelope(response: dict[str, Any]) -> dict[str, Any]:
    """Parse a v1 ResponseEnvelope and return its fields."""
    return {
        "requestId": response.get("requestId", ""),
        "ok": response.get("ok"),
        "code": response.get("code", ""),
        "message": response.get("message", ""),
        "data": response.get("data", {}),
    }
