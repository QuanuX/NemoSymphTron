#!/usr/bin/env python3
"""Integrate or detach the standalone NemoSymphTron qxctl extension capsule into an explicit Symphony installation prefix."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    p_reg = sub.add_parser("register", help="Register NemoSymphTron qxctl extension commands into an explicit Symphony prefix")
    p_reg.add_argument("--symphony-prefix", required=True, type=Path, help="Explicit Symphony installation prefix")
    p_reg.add_argument("--nemosymphtron-ctl", required=True, type=Path, help="Path to nemosymphtron-ctl binary")
    p_reg.add_argument("--extension-catalog", required=True, type=Path, help="Path to catalog/qxctl_extension_commands.json")
    p_reg.add_argument("--version", required=True, help="Explicit NemoSymphTron version")
    p_reg.add_argument("--tops-id", required=True, help="Explicit TOPS UUID scope")

    p_status = sub.add_parser("status", help="Inspect registered NemoSymphTron extension binding in a Symphony prefix")
    p_status.add_argument("--symphony-prefix", required=True, type=Path)

    p_unreg = sub.add_parser("unregister", help="Detach NemoSymphTron qxctl extension from a Symphony prefix")
    p_unreg.add_argument("--symphony-prefix", required=True, type=Path)

    args = parser.parse_args()
    sym_prefix = args.symphony_prefix.expanduser().resolve()
    ext_dir = sym_prefix / "etc" / "symphony" / "extensions.d"
    binding_file = ext_dir / "nemosymphtron.extension.json"

    if args.action == "register":
        ctl_bin = args.nemosymphtron_ctl.expanduser().resolve(strict=True)
        cat_file = args.extension_catalog.expanduser().resolve(strict=True)
        ctl_sha = sha256_file(ctl_bin)
        cat_sha = sha256_file(cat_file)

        completed = subprocess.run(
            [str(ctl_bin), "commands", "manifest", "--json"],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        )
        manifest = json.loads(completed.stdout)

        ext_dir.mkdir(parents=True, exist_ok=True)
        record = {
            "schema": "symphony.qxctl.extension-capsule.v1",
            "extension_name": "nemosymphtron",
            "version": args.version,
            "tops_id": args.tops_id,
            "symphony_prefix": str(sym_prefix),
            "owner_executable": str(ctl_bin),
            "owner_executable_sha256": ctl_sha,
            "extension_catalog_path": str(cat_file),
            "extension_catalog_sha256": cat_sha,
            "registry_digest": manifest["registry_digest"],
            "registered_commands_count": len(manifest.get("commands", [])),
            "command_namespaces": ["nst", "siv", "beads", "shv.nst-*", "scv.nst-*"],
        }
        tmp = binding_file.with_suffix(".tmp")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(record, stream, indent=2)
            stream.write("\n")
        tmp.replace(binding_file)
        print(json.dumps({"status": "registered", "binding_file": str(binding_file), "record": record}, indent=2))
        return 0

    if args.action == "status":
        if not binding_file.is_file():
            print(json.dumps({"status": "unregistered", "binding_file": str(binding_file)}, indent=2))
            return 1
        record = json.loads(binding_file.read_text(encoding="utf-8"))
        print(json.dumps({"status": "registered", "binding_file": str(binding_file), "record": record}, indent=2))
        return 0

    if args.action == "unregister":
        removed = binding_file.is_file()
        binding_file.unlink(missing_ok=True)
        print(json.dumps({"status": "unregistered", "removed": removed, "binding_file": str(binding_file)}, indent=2))
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
