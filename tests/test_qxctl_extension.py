#!/usr/bin/env python3
"""Verify standalone qxctl extension registration, status, and detachment without touching upstream Symphony."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    ctl_bin = Path(sys.argv[1]).resolve(strict=True)
    repo_root = Path(__file__).resolve().parent.parent
    ext_cat = (repo_root / "catalog" / "qxctl_extension_commands.json").resolve(strict=True)
    script = (repo_root / "scripts" / "integrate_qxctl_extension.py").resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="nst_qxctl_ext_") as tmp_str:
        sym_prefix = Path(tmp_str).resolve() / "symphony_install"
        tops_id = "33333333-4444-5555-6666-777777777777"

        reg_out = subprocess.check_output(
            [
                sys.executable,
                str(script),
                "register",
                "--symphony-prefix",
                str(sym_prefix),
                "--nemosymphtron-ctl",
                str(ctl_bin),
                "--extension-catalog",
                str(ext_cat),
                "--version",
                "0.2.0-alpha.1",
                "--tops-id",
                tops_id,
            ],
            text=True,
        )
        reg_json = json.loads(reg_out)
        assert reg_json["status"] == "registered"
        assert reg_json["record"]["registered_commands_count"] == 35

        st_out = subprocess.check_output(
            [sys.executable, str(script), "status", "--symphony-prefix", str(sym_prefix)],
            text=True,
        )
        assert json.loads(st_out)["status"] == "registered"

        unreg_out = subprocess.check_output(
            [sys.executable, str(script), "unregister", "--symphony-prefix", str(sym_prefix)],
            text=True,
        )
        assert json.loads(unreg_out)["status"] == "unregistered"

    print("test_qxctl_extension PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
