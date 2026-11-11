#!/usr/bin/env python3
"""Multi-agent cluster orchestration test: SHM Fast-Path + Protobuf Engine + Beads CAS Leases & P2P Bus."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run_ctl(ctl_bin: Path, args: list[str]) -> dict:
    out = subprocess.check_output([str(ctl_bin)] + args + ["--json"], text=True)
    return json.loads(out)


def main() -> int:
    _mcp_bin = Path(sys.argv[1]).resolve(strict=True)
    ctl_bin = Path(sys.argv[2]).resolve(strict=True)
    engine_bin = Path(sys.argv[3]).resolve(strict=True)
    fp_client_bin = Path(sys.argv[4]).resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="nst_cluster_") as tmp_str:
        tmp = Path(tmp_str).resolve()
        prefix_dir = tmp / "prefix"
        run_dir = prefix_dir / "run"
        run_dir.mkdir(parents=True, mode=0o700)
        run_dir.chmod(0o700)
        sock_path = run_dir / "nst_ipc.sock"
        shm_name = f"/nst_cl_{os.getpid() % 100000}"
        tops_id = "22222222-3333-4444-5555-666666666666"

        engine_proc = subprocess.Popen(
            [
                str(engine_bin),
                "--shm-name",
                shm_name,
                "--socket",
                str(sock_path),
                "--arch",
                "spark",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            deadline = time.monotonic() + 5.0
            while not sock_path.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert sock_path.exists(), "Engine socket failed to appear"

            # 1. Verify Sovereign Symphony Fast-Path client over POSIX SHM (Pool 1)
            fp_out = subprocess.check_output(
                [str(fp_client_bin), "--shm-name", shm_name, "--task", "mc", "--paths", "4096"],
                text=True,
                timeout=10,
            )
            fp_json = json.loads(fp_out)
            assert fp_json["ok"] is True
            assert fp_json["status"] == "completed"
            assert fp_json["var_99"] > 0.0

            # 2. Verify live engine connection via nemosymphtron-ctl nst status
            st = run_ctl(
                ctl_bin,
                [
                    "nst",
                    "status",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                ],
            )
            assert st["engine_live_connected"] is True

            # 3. Run Monte Carlo VaR over live engine IPC (Pool 2)
            spec_file = tmp / "mc_spec.json"
            spec_file.write_text(
                json.dumps(
                    {
                        "kernel_id": "MONTE_CARLO_VAR",
                        "num_paths": 8192,
                        "num_assets": 4,
                        "time_steps": 64,
                        "spot_price": 100.0,
                        "volatility": 0.22,
                    }
                ),
                encoding="utf-8",
            )
            qres = run_ctl(
                ctl_bin,
                [
                    "nst",
                    "quant",
                    "run",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    f"--spec={spec_file}",
                ],
            )
            assert qres["execution_mode"] == "live_engine_ipc"
            assert qres["status_code"] == 0
            assert qres["var_99"] > 0.0

            # 4. Multi-agent Beads CAS Lease checkout & conflict enforcement
            checkout1 = run_ctl(
                ctl_bin,
                [
                    "beads",
                    "leases",
                    "checkout",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    "--agent-id=claude-architect",
                    "--file=src/portfolio_kernel.cu",
                    "--duration-sec=60",
                    "--expected-sha256=sha256:aaaa",
                ],
            )
            assert "src/portfolio_kernel.cu" in checkout1["file_leases"]

            # Conflicting checkout by codex must fail non-zero
            conflict = subprocess.run(
                [
                    str(ctl_bin),
                    "beads",
                    "leases",
                    "checkout",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    "--agent-id=symphony-codex",
                    "--file=src/portfolio_kernel.cu",
                    "--duration-sec=60",
                    "--json",
                ],
                capture_output=True,
                text=True,
            )
            assert conflict.returncode != 0

            # Release by holder succeeds
            run_ctl(
                ctl_bin,
                [
                    "beads",
                    "leases",
                    "release",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    "--agent-id=claude-architect",
                    "--file=src/portfolio_kernel.cu",
                ],
            )

            # 5. Multi-agent P2P bus dispatch & poll
            run_ctl(
                ctl_bin,
                [
                    "beads",
                    "bus",
                    "dispatch",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    "--sender=claude-architect",
                    "--recipient=symphony-codex",
                    "--pattern=PIPELINE",
                    "--bead-id=bead-101",
                    "--payload={\"stage\":\"implement\"}",
                ],
            )
            polled = run_ctl(
                ctl_bin,
                [
                    "beads",
                    "bus",
                    "poll",
                    f"--prefix={prefix_dir}",
                    "--version=0.2.0-alpha.1",
                    f"--tops-id={tops_id}",
                    "--agent-id=symphony-codex",
                ],
            )
            assert len(polled["messages"]) == 1
            assert polled["messages"][0]["bead_id"] == "bead-101"
        finally:
            engine_proc.terminate()
            engine_proc.wait(timeout=5)

    print("test_multi_agent_cluster PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
