#!/usr/bin/env python3
"""Install the CPU experiment package and register NemoSymphTron with a local client."""
import argparse
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

VERSION = '0.2.0-beta.2.1'
REPO = 'QuanuX/NemoSymphTron'
MARKET = 'quanux-nemosymphtron'
FIRST_PROMPT = ('Call nemosymphtron_connection and nemosymphtron_commands_list. Report the bridge version '
                'and routable command count. If the Symphony plugin is installed, also call '
                'symphony_connection and identify its separate registry. Use read-only tools.')


def target_name():
    system, cpu = platform.system(), platform.machine().lower()
    if system == 'Darwin' and cpu in ('arm64', 'x86_64') and int(platform.mac_ver()[0].split('.')[0]) >= 15:
        return 'macos15-universal'
    if system == 'Linux' and cpu in ('x86_64', 'aarch64', 'arm64'):
        release = Path('/etc/os-release').read_text().splitlines()
        fields = dict(row.split('=', 1) for row in release if '=' in row)
        if fields.get('ID', '').strip('"') == 'ubuntu' and fields.get('VERSION_ID', '').strip('"') in ('24.04', '26.04'):
            return 'ubuntu24.04-' + ('x86_64' if cpu == 'x86_64' else 'arm64')
    raise ValueError('Prebuilt CPU packages support macOS 15+ (Intel or Apple Silicon) and Ubuntu 24.04/26.04 (x86-64 or ARM64). See the README for source builds.')


def fetch(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'NemoSymphTron-installer/' + VERSION})
    with urllib.request.urlopen(request, timeout=90) as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Release download exceeds the expected size limit')
    return data


def package_bytes(args, target):
    if args.archive:
        if not args.sha256 or not re.fullmatch(r'[0-9a-f]{64}', args.sha256):
            raise ValueError('--archive requires its 64-character SHA-256 using --sha256')
        data, expected = args.archive.read_bytes(), args.sha256
    else:
        if args.sha256:
            raise ValueError('--sha256 is used only with --archive')
        name = f'nemosymphtron-{VERSION}-{target}.mcpb'
        release = json.loads(fetch(f'https://api.github.com/repos/{REPO}/releases/tags/v{VERSION}', 2_000_000))
        asset = next((a for a in release['assets'] if a['name'] == name), None)
        if release['tag_name'] != 'v' + VERSION or release['draft'] or not asset or not re.fullmatch(r'sha256:[0-9a-f]{64}', asset.get('digest') or ''):
            raise ValueError('Public release and its GitHub asset digest are required')
        expected = asset['digest'].split(':')[1]
        data = fetch(f'https://github.com/{REPO}/releases/download/v{VERSION}/{name}', 64_000_000)
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('The archive SHA-256 differs from the release digest')
    return data


def unpack(data, new, target):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        info = json.loads(archive.read('release.json'))
        if info != {'version': VERSION, 'target': target, 'compute': 'cpu'}:
            raise ValueError('Unexpected native release version, target or compute backend')
        if sum(m.file_size for m in archive.infolist()) > 256_000_000:
            raise ValueError('Archive is too large to extract')
        for member in archive.infolist():
            name = PurePosixPath(member.filename)
            if name.is_absolute() or '..' in name.parts or stat.S_ISLNK(member.external_attr >> 16):
                raise ValueError('Invalid archive member')
            dest = new / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, dest.open('xb') as stream:
                shutil.copyfileobj(source, stream)
            dest.chmod(0o755 if name.parts[0] == 'bin' else 0o644)


def install_tree(staged, target):
    if target.is_symlink():
        raise ValueError(f'An existing symlink occupies {target}')
    if target.exists():
        for source in staged.rglob('*'):
            existing = target / source.relative_to(staged)
            if existing.is_symlink() or (source.is_file() and (not existing.is_file() or source.read_bytes() != existing.read_bytes())):
                raise ValueError(f'Existing file differs: {existing}. Select a separate --prefix or workspace.')
        return
    staged.rename(target)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def register(client, base, runtime, workspace, codex):
    profile = base / 'profiles' / runtime.name
    profile.mkdir(parents=True, exist_ok=True, mode=0o700)
    with tempfile.TemporaryDirectory(dir=profile) as tmp:
        generated = Path(tmp) / 'connection.json'
        subprocess.run([sys.executable, str(runtime / 'scripts/configure_connection.py'), '--qxctl', str(runtime / 'bin/nemosymphtron-ctl'), '--working-directory', str(profile), '--output', str(generated)], check=True)
        connection = profile / 'connection.json'
        if connection.is_symlink() or (connection.exists() and (connection.read_bytes() != generated.read_bytes() or stat.S_IMODE(connection.stat().st_mode) != 0o600)):
            raise ValueError(f'Existing profile differs from this read-only experiment: {connection}. Select a separate --prefix.')
        if not connection.exists():
            generated.rename(connection)
    # The local owner is queried before any client registration.
    mcp = runtime / 'bin/nemosymphtron-mcp'
    args = ['--config', str(connection), '--tool-catalog', str(runtime / 'catalog/tools.json')]
    requests = [dict(jsonrpc='2.0', id=1, method='initialize', params={'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'nemo-installer', 'version': VERSION}}),
                dict(jsonrpc='2.0', method='notifications/initialized'),
                dict(jsonrpc='2.0', id=2, method='tools/call', params={'name': 'nemosymphtron_connection', 'arguments': {}})]
    result = subprocess.run([str(mcp), *args], input=''.join(json.dumps(r) + '\n' for r in requests), capture_output=True, text=True, check=True, timeout=30)
    replies = {r['id']: r for line in result.stdout.splitlines() if line for r in [json.loads(line)] if 'id' in r}
    status = replies[2]['result']['structuredContent']
    if replies[1]['result']['serverInfo']['version'] != VERSION or status['bridge_version'] != VERSION or status['routable_commands'] != 35:
        raise ValueError('Native MCP initialization or registry verification failed')
    if client == 'codex':
        parent = base / 'marketplaces'
        parent.mkdir(parents=True, exist_ok=True)
        location = parent / runtime.name
    else:
        parent = workspace.resolve(strict=True) / '.agents/plugins'
        parent.mkdir(parents=True, exist_ok=True)
        location = parent / 'nemosymphtron'
    with tempfile.TemporaryDirectory(dir=parent) as tmp:
        tree = Path(tmp) / 'tree'
        plugin = tree / 'plugin' if client == 'codex' else tree
        plugin.mkdir(parents=True)
        for name in ('skills', 'rules', 'assets'):
            shutil.copytree(runtime / name, plugin / name)
        shutil.copy2(runtime / 'GEMINI.md', plugin / 'GEMINI.md')
        server = {'command': str(mcp), 'args': args}
        if client == 'codex':
            portable = {'$schema': 'https://agent-plugins.org/schemas/1.0.0/plugin.schema.json', 'name': 'nemosymphtron', 'version': VERSION,
                        'description': 'Experiment with NemoSymphTron alongside your Symphony engine using a local, read-only native connection.',
                        'author': {'name': 'QuanuX', 'url': 'https://quanux.org'}, 'homepage': f'https://github.com/{REPO}',
                        'extensions': {'com.openai': {'requires_local_executor': True, 'interface': {'displayName': 'NemoSymphTron', 'shortDescription': 'Explore NemoSymphTron alongside Symphony', 'supportURL': 'https://support.quanux.org', 'defaultPrompt': [FIRST_PROMPT]}}}}
            write_json(plugin / 'plugin.json', portable)
            write_json(plugin / 'mcp.json', {'mcpServers': {'nemosymphtron': {'type': 'stdio', **server}}})
            market = json.loads((runtime / '.agents/plugins/marketplace.json').read_text())
            market['plugins'][0]['source']['path'] = './plugin'
            write_json(tree / '.agents/plugins/marketplace.json', market)
        else:
            manifest = json.loads((runtime / 'plugin.json').read_text())
            manifest.pop('userConfig', None)
            write_json(plugin / 'plugin.json', manifest)
            write_json(plugin / 'mcp_config.json', {'mcpServers': {'nemosymphtron': server}})
        install_tree(tree, location)
    if client == 'codex':
        subprocess.run([codex, 'plugin', 'marketplace', 'add', str(location)], check=True)
        subprocess.run([codex, 'plugin', 'add', 'nemosymphtron@' + MARKET], check=True)
    print(f'Installed {client} plugin at {location}\nVerified {VERSION} and {status["routable_commands"]} native commands.\nStart a new {client} session.\n\n{FIRST_PROMPT}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True, choices=('codex', 'antigravity'))
    parser.add_argument('--workspace', type=Path, default=Path.cwd(), help='Antigravity workspace (default: current directory)')
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local/share/quanux/nemosymphtron')
    parser.add_argument('--codex', help='Path to a Codex CLI with plugin marketplace support')
    parser.add_argument('--archive', type=Path, help='Use a previously downloaded release MCPB')
    parser.add_argument('--sha256', help='SHA-256 from the release for --archive')
    args = parser.parse_args()
    target = target_name()
    codex = args.codex or shutil.which('codex')
    if args.client == 'codex' and not codex:
        raise ValueError('Install the Codex CLI or use --codex /path/to/codex')
    data = package_bytes(args, target)
    base = args.prefix.expanduser().resolve()
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (base / '.install.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        parent = base / 'releases'
        parent.mkdir(exist_ok=True)
        runtime = parent / (VERSION + '-' + target)
        with tempfile.TemporaryDirectory(dir=parent) as tmp:
            staged = Path(tmp) / 'release'
            staged.mkdir()
            unpack(data, staged, target)
            install_tree(staged, runtime)
        register(args.client, base, runtime, args.workspace, codex)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        sys.exit(f'NemoSymphTron installation: {exc}')
