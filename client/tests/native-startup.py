#!/usr/bin/env python3
"""Linux CI: real window/authentication, listener address, clean exit/port release."""
import os
from pathlib import Path
import socket
import subprocess
import time
import tempfile

# WebView child processes can inherit output descriptors. A temporary file avoids
# communicate() waiting on an inherited pipe after the application exits.
logs = tempfile.TemporaryFile(mode="w+t")
process = subprocess.Popen(["target/release/lapis-obsidian-client", "--smoke-test"], stdout=logs, stderr=logs, text=True)
ports = set()
deadline = time.monotonic() + 25
try:
    while process.poll() is None and time.monotonic() < deadline:
        try:
            inodes = {os.readlink(p)[8:-1] for p in Path(f"/proc/{process.pid}/fd").iterdir() if os.readlink(p).startswith("socket:[")}
            for family in ("tcp", "tcp6"):
                for line in Path(f"/proc/{process.pid}/net/{family}").read_text().splitlines()[1:]:
                    fields = line.split()
                    if fields[9] in inodes and fields[3] == "0A":
                        address, port = fields[1].split(":")
                        assert family == "tcp" and address == "0100007F", "Non-loopback listener"
                        ports.add(int(port, 16))
        except FileNotFoundError:
            pass
        time.sleep(.02)
    if process.poll() is None:
        process.kill()
        process.wait(timeout=3)
        logs.seek(0)
        raise AssertionError("Native startup timed out:\n" + logs.read())
    logs.seek(0)
    output = logs.read()
    assert process.returncode == 0, output
    assert "local WebView authenticated" in output, output
    assert ports, "No native bridge listener observed"
    for port in ports:
        with socket.socket() as probe:
            assert probe.connect_ex(("127.0.0.1", port)) != 0, "Bridge survived application exit"
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", port))
    print("Native startup: real WebView authenticated, loopback-only listener, clean exit and port release passed")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()

    logs.close()
