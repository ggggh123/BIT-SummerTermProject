"""既有v1大端长度JSON协议；报告只包含动作/代码/请求标识。"""
import json
import re
import socket
import struct
import time
import uuid
from datetime import datetime


class ProtocolError(Exception):
    pass


class Connection:
    def __init__(self, port, timeout):
        self.port = port
        self.deadline = time.monotonic() + timeout
        self.events = []

    def remaining(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise ProtocolError("TCP_TIMEOUT")
        return remaining

    def receive(self, sock, size):
        buffer = bytearray()
        while len(buffer) < size:
            sock.settimeout(self.remaining())
            chunk = sock.recv(size - len(buffer))
            if not chunk:
                raise ProtocolError("TCP_TRUNCATED")
            buffer.extend(chunk)
        return buffer

    def request(self, action, payload=None, token=""):
        request_id = str(uuid.uuid4())
        event = {"action": action, "code": "PROTOCOL_ERROR", "requestId": request_id}
        self.events.append(event)
        request = {"version": 1, "requestId": request_id, "action": action,
                   "token": token, "payload": payload or {}}
        body = json.dumps(request).encode()
        if len(body) > 1048576:
            raise ProtocolError("FRAME_TOO_LARGE")
        try:
            with socket.create_connection(("127.0.0.1", self.port), self.remaining()) as sock:
                sock.settimeout(self.remaining())
                sock.sendall(struct.pack(">I", len(body)) + body)
                size = struct.unpack(">I", self.receive(sock, 4))[0]
                if not 0 < size <= 1048576:
                    raise ProtocolError("INVALID_FRAME_SIZE")
                response = json.loads(self.receive(sock, size))
            if (not isinstance(response, dict) or response.get("requestId") != request_id
                    or type(response.get("ok")) is not bool
                    or not isinstance(response.get("code"), str)
                    or not isinstance(response.get("message"), str)
                    or not isinstance(response.get("data"), dict)):
                raise ProtocolError("INVALID_ENVELOPE")
            if re.fullmatch(r"[A-Z][A-Z0-9_]{0,63}", response["code"], flags=re.ASCII):
                event["code"] = response["code"]
            if response["ok"] is not True or response["code"] != "OK":
                raise ProtocolError("RESPONSE_REJECTED")
            return response["data"]
        except (OSError, ValueError, UnicodeError) as exc:
            raise ProtocolError("TCP_OR_JSON_ERROR") from exc

    def health(self):
        data = self.request("system.health")
        if (data.get("status") not in ("ready", "degraded") or type(data.get("schemaVersion")) is not int
                or data["schemaVersion"] != 1 or type(data.get("snapshotVersion")) is not int
                or data["snapshotVersion"] < 0 or "forecastRunId" not in data
                or not (data["forecastRunId"] is None or isinstance(data["forecastRunId"], str))
                or not isinstance(data.get("serverTime"), str)):
            raise ProtocolError("HEALTH_NOT_READY")
        try:
            timestamp = datetime.fromisoformat(data["serverTime"].replace("Z", "+00:00"))
            if timestamp.tzinfo is None:
                raise ValueError()
        except ValueError as exc:
            raise ProtocolError("HEALTH_NOT_READY") from exc
        return data

    def business_smoke(self):
        def identifier(value):
            return type(value) is int and value > 0

        def user_shape(user):
            return (isinstance(user, dict) and identifier(user.get("userId"))
                    and user.get("mobile") == "13800138000")

        login = self.request("auth.user_login", {"mobile": "13800138000"})
        token = login.get("token")
        user = login.get("user")
        if not isinstance(token, str) or not token.strip() or not user_shape(user):
            raise ProtocolError("LOGIN_SHAPE_INVALID")
        user_id = user["userId"]
        current = self.request("user.get", token=token).get("user")
        if not user_shape(current) or current["userId"] != user_id:
            raise ProtocolError("USER_OWNERSHIP_INVALID")
        stations = self.request("station.list", token=token).get("stations")
        if (not isinstance(stations, list) or not stations
                or any(not isinstance(station, dict) or not identifier(station.get("stationId")) for station in stations)):
            raise ProtocolError("STATIONS_SHAPE_INVALID")
        station_id = stations[0]["stationId"]
        detail = self.request("station.detail", {"stationId": station_id}, token)
        station, chargers = detail.get("station"), detail.get("chargers")
        if (not isinstance(station, dict) or station.get("stationId") != station_id
                or not isinstance(chargers, list) or not chargers
                or any(not isinstance(charger, dict) or not identifier(charger.get("chargerId"))
                       or charger.get("stationId") != station_id for charger in chargers)):
            raise ProtocolError("STATION_OWNERSHIP_INVALID")
        order_data = self.request("order.current", token=token)
        if "order" not in order_data:
            raise ProtocolError("ORDER_SHAPE_INVALID")
        order = order_data["order"]
        if order is not None and (not isinstance(order, dict) or order.get("userId") != user_id
                                  or order.get("status") not in ("reserved", "charging")):
            raise ProtocolError("ORDER_OWNERSHIP_INVALID")
