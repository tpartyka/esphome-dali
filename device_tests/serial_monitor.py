"""Background USB-serial log watcher for the stability test tier.

Reads lines from the device's USB console in a background thread, keeps a
ring buffer, and flags lines matching known crash/watchdog patterns so tests
can assert the device survived a flood without rebooting or asserting.
"""

import re
import threading
from collections import deque

import serial

CRASH_PATTERNS = [
    re.compile(r"Guru Meditation Error"),
    re.compile(r"abort\(\) was called"),
    re.compile(r"Task watchdog got triggered"),
    re.compile(r"Backtrace:"),
    re.compile(r"CORRUPT HEAP"),
    re.compile(r"rst:0x.*\(TG\dWDT_SYS_RESET\)"),
]


class SerialLogWatcher:
    """Reads `port` in a background thread and records matched crash lines."""

    def __init__(self, port: str, baud: int, max_lines: int = 5000):
        self._port = port
        self._baud = baud
        self._lines: deque[str] = deque(maxlen=max_lines)
        self._crash_lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._ser: serial.Serial | None = None
        self._thread: threading.Thread | None = None

    def __enter__(self) -> "SerialLogWatcher":
        self._ser = serial.Serial(self._port, self._baud, timeout=1)
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *exc_info) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
        if self._ser is not None:
            self._ser.close()

    def _run(self) -> None:
        assert self._ser is not None
        while not self._stop.is_set():
            try:
                raw = self._ser.readline()
            except serial.SerialException:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            with self._lock:
                self._lines.append(line)
                if any(p.search(line) for p in CRASH_PATTERNS):
                    self._crash_lines.append(line)

    def lines(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def assert_no_crash(self) -> None:
        with self._lock:
            crashes = list(self._crash_lines)
        if crashes:
            joined = "\n".join(crashes)
            raise AssertionError(f"Device crash pattern(s) detected:\n{joined}")
