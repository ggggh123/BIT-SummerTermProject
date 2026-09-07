"""Linux进程身份核对；所有信号仅经pidfd发送。"""
import os
from pathlib import Path
import select
import signal


def capture(pid):
    if type(pid) is not int or pid <= 0:
        raise ValueError("invalid pid")
    proc = Path("/proc") / str(pid)
    # comm可以包含空格及括号，从最后一个右括号之后读取固定字段。
    stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    if stat[0] in ("Z", "X"):
        raise ProcessLookupError(pid)
    return {"pid": pid, "starttime": stat[19],
            "exe": os.readlink(proc / "exe"),
            "bootId": Path("/proc/sys/kernel/random/boot_id").read_text().strip()}


def inspect(record):
    try:
        current = capture(record["pid"])
    except (FileNotFoundError, ProcessLookupError):
        return "EXITED"
    except (OSError, ValueError, KeyError, TypeError):
        return "WRONG_PID"
    return "ALIVE" if all(current[key] == record.get(key) for key in current) else "WRONG_PID"


def supported():
    return hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal")


def stop_one(record, timeout, force=False):
    if not supported():
        return "PIDFD_UNSUPPORTED"
    state = inspect(record)
    if state != "ALIVE":
        return state
    try:
        fd = os.pidfd_open(record["pid"])
    except ProcessLookupError:
        return "EXITED"
    except OSError:
        return "PIDFD_UNSUPPORTED"
    try:
        # pidfd先锁定，再读/proc，杜绝检查到发信号之间PID复用。
        state = inspect(record)
        if state != "ALIVE":
            return state
        poll = select.poll()
        poll.register(fd, select.POLLIN)
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        if poll.poll(int(timeout * 1000)):
            return "EXITED"
        if not force:
            return "STOP_TIMEOUT"
        state = inspect(record)
        if state != "ALIVE":
            return state
        signal.pidfd_send_signal(fd, signal.SIGKILL)
        return "EXITED" if poll.poll(int(timeout * 1000)) else "STOP_TIMEOUT"
    except ProcessLookupError:
        return "EXITED"
    except OSError:
        return "SIGNAL_FAILED"
    finally:
        os.close(fd)


def stop_all(records, timeout, force=False):
    return {role: stop_one(records[role], timeout, force)
            for role in ("client", "simulator", "server") if role in records}
