#!/usr/bin/env python3
"""Emit host MCP client configuration fragments for NemoSymphTron without modifying user dotfiles unless --output is selected."""

import argparse
import json
import os
from pathlib import Path

CLIENTS = ("antigravity", "antigravity-cli", "gemini-cli", "codex", "claude", "generic")


def toml_string(value: str) -> str:
    return json.dumps(value)


def validate_path(raw: str, flag: str) -> str:
    if not raw or not raw.startswith("/") or raw == "/" or "\n" in raw or "\r" in raw or "\x00" in raw:
        raise SystemExit(f"{flag} must be a non-root absolute path without control characters")
    parts = Path(raw).parts
    if ".." in parts or "." in parts:
        raise SystemExit(f"{flag} must not contain relative traversal components")
    return raw


def render(client: str, binary: str, mode_flag: str, target: str, catalog: str | None = None) -> str:
    args = [mode_flag, target]
    if catalog is not None:
        args += ["--tool-catalog", catalog]
    if client == "codex":
        rendered_args = ", ".join(toml_string(item) for item in args)
        return (
            "[mcp_servers.nemosymphtron]\n"
            f"command = {toml_string(binary)}\n"
            f"args = [{rendered_args}]\n"
            "startup_timeout_sec = 70\n"
            "tool_timeout_sec = 70\n"
            'default_tools_approval_mode = "writes"\n'
        )
    server: dict[str, object] = {"command": binary, "args": args}
    if client == "claude":
        server = {"type": "stdio", **server}
    return json.dumps({"mcpServers": {"nemosymphtron": server}}, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", required=True, choices=CLIENTS)
    parser.add_argument("--binary", required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--config", help="Absolute path to connection.json for direct stdio")
    mode.add_argument("--connect-socket", help="Absolute path to private Unix domain socket for IPC relay")
    parser.add_argument("--tool-catalog", help="Absolute path to catalog/tools.json")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    binary = validate_path(args.binary, "--binary")
    catalog = validate_path(args.tool_catalog, "--tool-catalog") if args.tool_catalog else None
    if args.config is not None:
        target = validate_path(args.config, "--config")
        text = render(args.client, binary, "--config", target, catalog)
    else:
        if catalog is not None:
            raise SystemExit("--tool-catalog belongs on the IPC listener, not --connect-socket")
        target = validate_path(args.connect_socket, "--connect-socket")
        text = render(args.client, binary, "--connect-socket", target)

    if args.output:
        output = args.output
        if output.is_symlink() or output.exists():
            raise SystemExit("Refusing to overwrite existing path or symlink")
        output.parent.mkdir(parents=True, exist_ok=True)
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        fd = os.open(output, flags, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            os.fchmod(stream.fileno(), 0o600)
            stream.write(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
