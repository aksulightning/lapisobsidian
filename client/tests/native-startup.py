#!/usr/bin/env python3
"""Linux CI: real window/authentication, listener address, clean exit/port release."""
import re
import socket
import subprocess
import tempfile

# WebView children can inherit output descriptors. A temporary file avoids
# waiting on their output pipes, and does not require reading a sandboxed
# process's /proc/fd directory. The app's smoke diagnostics never contain tokens.
with tempfile.TemporaryFile(mode="w+t") as logs:
    process = subprocess.Popen(
        ["target/release/lapis-obsidian-client", "--smoke-test"],
        stdout=logs, stderr=logs, text=True,
    )
    try:
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
            logs.seek(0)
            raise AssertionError("Native startup timed out:\n" + logs.read())
        logs.seek(0)
        output = logs.read()
        assert process.returncode == 0, output
        assert "local WebView authenticated" in output, output
        assert "local bridge stopped" in output, output
        addresses = re.findall(r"Startup check: loopback bridge (\S+)", output)
        assert len(addresses) == 1, output
        match = re.fullmatch(r"http://127\.0\.0\.1:(\d+)", addresses[0])
        assert match, "Unexpected bridge bind address"
        port = int(match.group(1))
        with socket.socket() as probe:
            assert probe.connect_ex(("127.0.0.1", port)) != 0, "Bridge survived exit"
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(("127.0.0.1", port))
        print("Native startup: real WebView authenticated, loopback listener, clean exit and port release passed")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
