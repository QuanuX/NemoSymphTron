#!/usr/bin/env python3
"""End-to-end MCP 2025-11-25 JSON-RPC gateway & preview/apply verification for nemosymphtron-mcp."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rpc_send(proc: subprocess.Popen, payload: dict) -> None:
    assert proc.stdin is not None
    proc.stdin.write(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
    proc.stdin.flush()


def rpc_recv(proc: subprocess.Popen) -> dict:
    assert proc.stdout is not None
    line = proc.stdout.readline()
    if not line:
        err = proc.stderr.read().decode("utf-8", errors="replace") if proc.stderr else ""
        raise RuntimeError(f"Unexpected EOF from nemosymphtron-mcp: {err}")
    return json.loads(line.decode("utf-8"))


def main() -> int:
    mcp_bin = Path(sys.argv[1]).resolve(strict=True)
    ctl_bin = Path(sys.argv[2]).resolve(strict=True)
    repo_root = Path(__file__).resolve().parent.parent
    catalog_path = (repo_root / "catalog" / "tools.json").resolve(strict=True)
    cfg_script = (repo_root / "scripts" / "configure_connection.py").resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="nst_mcp_test_") as tmp_str:
        tmp = Path(tmp_str).resolve()
        conn_path = tmp / "connection.json"
        prefix_dir = tmp / "prefix"
        prefix_dir.mkdir(parents=True, exist_ok=True)
        tops_id = "11111111-2222-3333-4444-555555555555"

        subprocess.run(
            [
                sys.executable,
                str(cfg_script),
                "--qxctl",
                str(ctl_bin),
                "--working-directory",
                str(tmp),
                "--output",
                str(conn_path),
                "--enable-all-nonread",
            ],
            check=True,
        )
        assert (conn_path.stat().st_mode & 0o777) == 0o600

        proc = subprocess.Popen(
            [str(mcp_bin), "--config", str(conn_path), "--tool-catalog", str(catalog_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {
                        "protocolVersion": "2025-11-25",
                        "capabilities": {},
                        "clientInfo": {"name": "nst-test", "version": "0.2.0"},
                    },
                },
            )
            init_res = rpc_recv(proc)
            assert init_res["result"]["protocolVersion"] == "2025-11-25"
            rpc_send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})

            # 1. Verify nemosymphtron_connection
            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "tools/call",
                    "params": {"name": "nemosymphtron_connection", "arguments": {}},
                },
            )
            conn_res = rpc_recv(proc)["result"]
            assert not conn_res["isError"]
            sc = conn_res["structuredContent"]
            assert sc["routable_commands"] == 35

            # 2. Verify read-only nemosymphtron_nst_status
            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "tools/call",
                    "params": {
                        "name": "nemosymphtron_nst_status",
                        "arguments": {
                            "prefix": str(prefix_dir),
                            "version": "0.2.0-alpha.1",
                            "tops-id": tops_id,
                        },
                    },
                },
            )
            st_res = rpc_recv(proc)["result"]
            assert not st_res["isError"]
            assert st_res["structuredContent"]["semantic_success"] == "not_asserted"
            assert st_res["structuredContent"]["parsed_stdout"]["readiness"] == "READY"
            assert st_res["structuredContent"]["parsed_stdout"]["state_digest"] == "absent"

            # 3. Verify single-use preview -> apply for shv nst-lock
            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "tools/call",
                    "params": {
                        "name": "nemosymphtron_shv_nst_lock_preview",
                        "arguments": {
                            "prefix": str(prefix_dir),
                            "version": "0.2.0-alpha.1",
                            "tops-id": tops_id,
                            "expected-state-digest": "absent",
                        },
                    },
                },
            )
            prev_res = rpc_recv(proc)["result"]
            assert not prev_res["isError"]
            plan_id = prev_res["structuredContent"]["plan_id"]
            assert prev_res["structuredContent"]["owner_plan_executed"] is False
            assert prev_res["structuredContent"]["authorization_granted"] is False

            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 5,
                    "method": "tools/call",
                    "params": {
                        "name": "nemosymphtron_shv_nst_lock_apply",
                        "arguments": {"plan_id": plan_id},
                    },
                },
            )
            app_res = rpc_recv(proc)["result"]
            assert not app_res["isError"]
            assert app_res["structuredContent"]["parsed_stdout"]["hardware_locked"] is True

            # Re-using consumed plan_id must fail closed
            rpc_send(
                proc,
                {
                    "jsonrpc": "2.0",
                    "id": 6,
                    "method": "tools/call",
                    "params": {
                        "name": "nemosymphtron_shv_nst_lock_apply",
                        "arguments": {"plan_id": plan_id},
                    },
                },
            )
            replay_res = rpc_recv(proc)["result"]
            assert replay_res["isError"] is True
        finally:
            if proc.stdin:
                proc.stdin.close()
            proc.terminate()
            proc.wait(timeout=5)

    print("test_mcp_gateway PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
