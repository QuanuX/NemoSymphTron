#!/usr/bin/env python3
"""Package a tested native build and its host resources as a deterministic MCPB."""
import argparse
import json
from pathlib import Path
import stat
import platform
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BINARIES = ('nemosymphtron-mcp', 'nemosymphtron-ctl', 'nemosymphtron-engine', 'symphony-fastpath-client')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--target', choices=['macos15-universal', 'ubuntu24.04-x86_64', 'ubuntu24.04-arm64'], required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'packaging/mcpb/manifest.json').read_text())
    manifest['compatibility']['platforms'] = ['darwin' if args.target.startswith('macos') else 'linux']
    version = manifest['version']
    if args.target == 'macos15-universal':
        for binary in BINARIES:
            subprocess.run(['lipo', str(args.build / binary), '-verify_arch', 'x86_64', 'arm64'], check=True)
    else:
        machine = platform.machine()
        expected = 'arm64' if machine in ('aarch64', 'arm64') else machine
        if platform.system() != 'Linux' or args.target != 'ubuntu24.04-' + expected:
            raise ValueError('The release target must match the native Linux build runner')
    reported = subprocess.check_output([str(args.build.resolve() / 'nemosymphtron-mcp'), '--version'], text=True).strip()
    if reported != 'nemosymphtron-mcp ' + version:
        raise ValueError('The gateway and package manifest versions differ')
    files = {name: (args.build / name).read_bytes() for name in BINARIES}
    members = {'bin/' + k: v for k, v in files.items()}
    members['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    members['release.json'] = (json.dumps({'version': version, 'target': args.target, 'compute': 'cpu'}, indent=2) + '\n').encode()
    static = ['LICENSE', 'README.md', 'plugin.json', 'GEMINI.md', 'scripts/configure_connection.py', 'scripts/integrate_qxctl_extension.py', 'scripts/configure_clients.py', '.codex-plugin/plugin.json', '.agents/plugins/marketplace.json']
    for folder in ('assets', 'catalog', 'skills', 'rules', 'templates', 'schemas'):
        static.extend(p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*') if p.is_file())
    members.update((name, (ROOT / name).read_bytes()) for name in static)
    args.out.mkdir(parents=True, exist_ok=True)
    dest = args.out / f'nemosymphtron-{version}-{args.target}.mcpb'
    with zipfile.ZipFile(dest, 'w') as archive:
        for name, data in sorted(members.items()):
            item = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            item.create_system = 3
            mode = 0o755 if name.startswith('bin/') else 0o644
            item.external_attr = (stat.S_IFREG | mode) << 16
            archive.writestr(item, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    print(dest)


if __name__ == '__main__':
    main()
