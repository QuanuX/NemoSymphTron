#!/usr/bin/env python3
"""Manage the optional same-user Unix-socket IPC listener (--listen / --connect-socket) for NemoSymphTron."""

import argparse
import json
import os
from pathlib import Path
import select
import signal
import stat
import subprocess
import time


def validate_private_socket_path(raw: Path) -> Path:
    expanded = raw.expanduser()
    if not expanded.is_absolute():
        raise SystemExit("--socket must be an absolute path")
    if expanded.is_symlink():
        raise SystemExit("--socket must not be a symlink")
    parent = expanded.parent
    if not parent.exists():
        parent.mkdir(parents=True, mode=0o700)
        parent.chmod(0o700)
    if parent.is_symlink():
        raise SystemExit("--socket parent directory must not be a symlink")
    pst = parent.lstat()
    if not stat.S_ISDIR(pst.st_mode):
        raise SystemExit("--socket parent must be a directory")
    if pst.st_uid != os.getuid() or (pst.st_mode & 0o077) != 0:
        raise SystemExit("--socket parent directory must be owned by the current user with mode 0700")
    return parent.resolve(strict=True) / expanded.name


def probe_relay(binary: Path, socket_path: Path, timeout: float = 5.0) -> dict:
    proc = subprocess.Popen(
        [str(binary), "--connect-socket", str(socket_path)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    try:
        def send(msg: dict) -> None:
            assert proc.stdin is not None
            proc.stdin.write(json.dumps(msg, separators=(",", ":")).encode("utf-8") + b"\n")
            proc.stdin.flush()

        pending = b""

        def recv() -> dict:
            nonlocal pending
            assert proc.stdout is not None
            deadline = time.monotonic() + timeout
            while b"\n" not in pending:
                rem = deadline - time.monotonic()
                if rem <= 0 or not select.select([proc.stdout], [], [], rem)[0]:
                    raise RuntimeError("IPC relay probe timed out")
                chunk = os.read(proc.stdout.fileno(), 65536)
                if not chunk:
                    err = proc.stderr.read().decode("utf-8", errors="replace") if proc.stderr else ""
                    raise RuntimeError(f"IPC relay closed unexpectedly: {err.strip()}")
                pending += chunk
            line, pending = pending.split(b"\n", 1)
            return json.loads(line)

        send({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "nemosymphtron-ipc-probe", "version": "0.2.0-alpha.1"},
            },
        })
        init_resp = recv()
        if "result" not in init_resp:
            raise RuntimeError(f"Initialize failed: {init_resp}")
        send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        send({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "nemosymphtron_connection", "arguments": {}}})
        conn_resp = recv()
        if "result" not in conn_resp or conn_resp["result"].get("isError"):
            raise RuntimeError(f"nemosymphtron_connection failed: {conn_resp}")
        return conn_resp["result"]["structuredContent"]
    finally:
        if proc.stdin and not proc.stdin.closed:
            proc.stdin.close()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=2)
        for stream in (proc.stdout, proc.stderr):
            if stream and not stream.closed:
                stream.close()


def cmd_start(args: argparse.Namespace) -> int:
    socket_path = validate_private_socket_path(args.socket)
    if socket_path.exists():
        raise SystemExit(f"Socket path already exists: {socket_path}")
    binary = args.binary.expanduser().resolve(strict=True)
    config = args.config.expanduser().resolve(strict=True)
    argv = [str(binary), "--config", str(config)]
    if args.tool_catalog is not None:
        catalog = args.tool_catalog.expanduser().resolve(strict=True)
        argv += ["--tool-catalog", str(catalog)]
    argv += ["--listen", str(socket_path)]

    pid_file = socket_path.with_suffix(".pid")
    log_file = socket_path.with_suffix(".log")
    with log_file.open("ab") as err_stream:
        os.fchmod(err_stream.fileno(), 0o600)
        proc = subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=err_stream,
            start_new_session=True,
        )

    deadline = time.monotonic() + 5.0
    while not socket_path.exists() and proc.poll() is None and time.monotonic() < deadline:
        time.sleep(0.02)

    if proc.poll() is not None or not socket_path.exists():
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=2)
        details = log_file.read_text(encoding="utf-8", errors="replace").strip() if log_file.exists() else ""
        raise SystemExit(f"IPC listener failed to start: {details}")

    pid_file.write_text(f"{proc.pid}\n", encoding="utf-8")
    pid_file.chmod(0o600)
    print(json.dumps({
        "status": "listening",
        "pid": proc.pid,
        "socket": str(socket_path),
        "socket_mode": oct(socket_path.lstat().st_mode & 0o777),
        "antigravity_mcp_config": {
            "mcpServers": {
                "nemosymphtron": {
                    "command": str(binary),
                    "args": ["--connect-socket", str(socket_path)],
                }
            }
        },
    }, indent=2))
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    socket_path = validate_private_socket_path(args.socket)
    pid_file = socket_path.with_suffix(".pid")
    exists = socket_path.exists()
    is_sock = False
    mode_str = None
    if exists:
        st = socket_path.lstat()
        is_sock = stat.S_ISSOCK(st.st_mode)
        mode_str = oct(st.st_mode & 0o777)
    pid = int(pid_file.read_text(encoding="utf-8").strip()) if pid_file.is_file() else None
    report: dict[str, object] = {
        "socket": str(socket_path),
        "exists": exists,
        "is_socket": is_sock,
        "socket_mode": mode_str,
        "pid": pid,
    }
    if args.probe:
        if args.binary is None:
            raise SystemExit("--probe requires --binary")
        if not is_sock:
            raise SystemExit("Cannot probe: socket endpoint is not active")
        binary = args.binary.expanduser().resolve(strict=True)
        report["connection_probe"] = probe_relay(binary, socket_path)
    print(json.dumps(report, indent=2))
    return 0 if is_sock else 1


def cmd_stop(args: argparse.Namespace) -> int:
    socket_path = validate_private_socket_path(args.socket)
    pid_file = socket_path.with_suffix(".pid")
    if not pid_file.is_file():
        raise SystemExit(f"No PID file found at {pid_file}")
    pid = int(pid_file.read_text(encoding="utf-8").strip())
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + 5.0
    while socket_path.exists() and time.monotonic() < deadline:
        time.sleep(0.02)
    pid_file.unlink(missing_ok=True)
    print(json.dumps({"status": "stopped", "pid": pid, "socket_removed": not socket_path.exists()}, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    p_start = sub.add_parser("start", help="Start the local Unix-socket IPC listener in a mode-0700 directory")
    p_start.add_argument("--binary", required=True, type=Path)
    p_start.add_argument("--config", required=True, type=Path)
    p_start.add_argument("--tool-catalog", type=Path)
    p_start.add_argument("--socket", required=True, type=Path)

    p_status = sub.add_parser("status", help="Inspect the local Unix-socket IPC endpoint and optionally probe over MCP")
    p_status.add_argument("--socket", required=True, type=Path)
    p_status.add_argument("--binary", type=Path)
    p_status.add_argument("--probe", action="store_true")

    p_stop = sub.add_parser("stop", help="Stop the local Unix-socket IPC listener and verify socket removal")
    p_stop.add_argument("--socket", required=True, type=Path)

    args = parser.parse_args()
    if args.action == "start":
        return cmd_start(args)
    if args.action == "status":
        return cmd_status(args)
    return cmd_stop(args)


if __name__ == "__main__":
    raise SystemExit(main())
