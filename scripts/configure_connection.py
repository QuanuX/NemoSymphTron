#!/usr/bin/env python3
"""Create a mode-0600 NemoSymphTron MCP connection profile from an explicit qxctl / nemosymphtron-ctl path."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ENV_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NONREAD = {"permission_backed_mutation", "proposal_only", "evidence_only"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def parse_env(entries: list[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"Expected KEY=VALUE environment entry, got: {entry}")
        key, value = entry.split("=", 1)
        if not ENV_NAME.match(key) or "\x00" in value:
            raise ValueError(f"Invalid environment variable name: {key}")
        values[key] = value
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qxctl", required=True, type=Path, help="Absolute or resolvable path to nemosymphtron-ctl or qxctl")
    parser.add_argument("--working-directory", required=True, type=Path, help="Explicit working directory")
    parser.add_argument("--output", required=True, type=Path, help="Output connection.json path")
    parser.add_argument("--enable-nonread", action="append", default=[], help="Command ID to allow through prepare/apply")
    parser.add_argument("--enable-all-nonread", action="store_true", help="Enable all observed non-read commands in the profile")
    parser.add_argument("--env", action="append", default=[], help="Explicit KEY=VALUE environment entry")
    parser.add_argument("--inherit-env", action="append", default=["HOME", "TMPDIR"], help="Environment name to inherit")
    parser.add_argument("--timeout-ms", type=int, default=30000)
    parser.add_argument("--max-output-bytes", type=int, default=524288)
    parser.add_argument("--force", action="store_true", help="Replace an existing profile file")
    args = parser.parse_args()

    qxctl = args.qxctl.expanduser().resolve(strict=True)
    workdir = args.working_directory.expanduser().resolve(strict=True)
    output = args.output.expanduser().resolve()
    if not workdir.is_dir():
        raise SystemExit("--working-directory must be an existing directory")
    if not (100 <= args.timeout_ms <= 60000 and 1024 <= args.max_output_bytes <= 8 * 1024 * 1024):
        raise SystemExit("Invalid timeout or output limit")
    for name in args.inherit_env:
        if not ENV_NAME.match(name):
            raise SystemExit(f"Invalid inherited environment variable name: {name}")
    explicit_env = parse_env(args.env)

    qxctl_digest = sha256_file(qxctl)
    child_env = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LANG": "C.UTF-8"}
    for name in args.inherit_env:
        if name in os.environ:
            child_env[name] = os.environ[name]
    child_env.update(explicit_env)

    completed = subprocess.run(
        [str(qxctl), "commands", "manifest", "--json"],
        cwd=str(workdir),
        env=child_env,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=max(args.timeout_ms / 1000, 5),
        check=True,
    )
    manifest = json.loads(completed.stdout)
    if manifest.get("protocol") != "symphony.qxctl.command-registry.v1" or manifest.get("registry_kind") != "observed":
        raise SystemExit("Executable did not return an observed v1 command registry")
    if manifest.get("executable_digest") != qxctl_digest:
        raise SystemExit("Manifest executable_digest did not match the selected binary")
    after_digest = sha256_file(qxctl)
    if after_digest != qxctl_digest:
        raise SystemExit("Binary changed while inspecting its manifest")
    by_id = {item["command_id"]: item for item in manifest.get("commands", [])}
    enabled_ids = list(args.enable_nonread)
    if args.enable_all_nonread:
        for cid, rec in by_id.items():
            if rec.get("mutability") in NONREAD and rec.get("visibility") == "public" and rec.get("noninteractive"):
                enabled_ids.append(cid)

    for command_id in enabled_ids:
        record = by_id.get(command_id)
        if not record or record.get("visibility") != "public" or not record.get("noninteractive"):
            raise SystemExit(f"Unknown or non-routable command ID: {command_id}")
        if record.get("mutability") not in NONREAD:
            raise SystemExit(f"Only non-read commands belong in --enable-nonread: {command_id}")

    profile = {
        "schema": "symphony.agent.connection.v1",
        "qxctl": str(qxctl),
        "qxctl_sha256": qxctl_digest,
        "working_directory": str(workdir),
        "registry_digest": manifest["registry_digest"],
        "enabled_nonread_commands": sorted(set(enabled_ids)),
        "environment": explicit_env,
        "inherit_environment": list(dict.fromkeys(args.inherit_env)),
        "timeout_ms": args.timeout_ms,
        "max_output_bytes": args.max_output_bytes,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if args.force else os.O_EXCL)
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(output, flags, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        os.fchmod(stream.fileno(), 0o600)
        json.dump(profile, stream, indent=2)
        stream.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
