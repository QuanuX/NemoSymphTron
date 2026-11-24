"""Exercise the published universal archive on a native Apple Silicon runner."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = 'QuanuX/NemoSymphTron'


def download(url, limit):
    req = urllib.request.Request(url, headers={'User-Agent': 'NemoSymphTron-release-check'})
    with urllib.request.urlopen(req, timeout=60) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f'Release download exceeds {limit} bytes')
    return data


def fetch_release(tag, directory):
    version = tag[1:]
    archive_name = f'nemosymphtron-{version}-macos15-universal.mcpb'
    release = json.loads(download(f'https://api.github.com/repos/{REPO}/releases/tags/{tag}', 2_000_000))
    if release['tag_name'] != tag or release['draft']:
        raise ValueError('A published release matching the requested tag is required')
    assets = {asset['name']: asset for asset in release['assets']}
    hashes = {}
    for name in ['SHA256SUMS', 'install-nemosymphtron.py', archive_name]:
        data = download(f'https://github.com/{REPO}/releases/download/{tag}/{name}', 64_000_000)
        digest = hashlib.sha256(data).hexdigest()
        if assets[name]['digest'] != 'sha256:' + digest:
            raise ValueError(f'GitHub release digest mismatch for {name}')
        hashes[name] = digest
        (directory / name).write_bytes(data)
    checksums = {}
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        checksums[name] = digest
    for name in ['install-nemosymphtron.py', archive_name]:
        if checksums.get(name) != hashes[name]:
            raise ValueError(f'Published SHA256SUMS differs for {name}')
    return directory / archive_name, hashes


def run_rpc(server, requests):
    lines = [json.dumps(message) + '\n' for message in requests]
    result = subprocess.run([server['command'], *server['args']], input=''.join(lines),
                            text=True, capture_output=True, check=True, timeout=45)
    return {row['id']: row['result'] for row in map(json.loads, result.stdout.splitlines())}


def verify_native(archive, workspace, version):
    plugin = workspace / '.agents/plugins/nemosymphtron'
    server = json.loads((plugin / 'mcp_config.json').read_text())['mcpServers']['nemosymphtron']
    gateway = Path(server['command'])
    profile_path = Path(server['args'][server['args'].index('--config') + 1])
    profile = json.loads(profile_path.read_text())
    owner = Path(profile['qxctl'])
    assert profile_path.stat().st_mode & 0o777 == 0o600
    assert profile['enabled_nonread_commands'] == []
    slices = {}
    with zipfile.ZipFile(archive) as package:
        for binary in [owner, gateway]:
            assert binary.read_bytes() == package.read('bin/' + binary.name)
            subprocess.run(['lipo', str(binary), '-verify_arch', 'arm64', 'x86_64'], check=True)
            thin = workspace / (binary.name + '.arm64')
            subprocess.run(['lipo', str(binary), '-thin', 'arm64', '-output', str(thin)], check=True)
            slices[binary.name] = hashlib.sha256(thin.read_bytes()).hexdigest()
            # arch explicitly selects the released ARM slice in both version probes.
            reported = subprocess.check_output(['arch', '-arm64', str(binary), '--version'], text=True).strip()
            assert reported == binary.name + ' ' + version
    registry = json.loads(subprocess.check_output(['arch', '-arm64', str(owner), 'commands', 'manifest', '--json']))
    assert registry['client_version'] == version
    assert registry['executable_digest'] == 'sha256:' + hashlib.sha256(owner.read_bytes()).hexdigest()
    native = {'command': 'arch', 'args': ['-arm64', str(gateway), *server['args']]}
    def request(i, method, params):
        return {'jsonrpc': '2.0', 'id': i, 'method': method, 'params': params}
    messages = [
        request(1, 'initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                  'clientInfo': {'name': 'published-apple-silicon', 'version': version}}),
        {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
        request(2, 'tools/call', {'name': 'nemosymphtron_connection', 'arguments': {}}),
        request(3, 'tools/call', {'name': 'nemosymphtron_commands_list', 'arguments': {}}),
    ]
    replies = run_rpc(native, messages)
    assert replies[1]['serverInfo']['version'] == version
    assert not replies[2]['isError'] and not replies[3]['isError']
    info, listing = replies[2]['structuredContent'], replies[3]['structuredContent']
    count = len(registry['commands'])
    assert count > 0 and info['routable_commands'] == count and info['enabled_nonread_commands'] == []
    assert listing['exit_code'] == 0 and listing['parsed_stdout']['commands'] == registry['commands']
    assert info['registry_digest'] == registry['registry_digest'] == profile['registry_digest']
    return {'arm64_slice_sha256': slices, 'native_version': version, 'routable_commands': count,
            'registry_digest': registry['registry_digest'], 'enabled_nonread_commands': []}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('receipt', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('A native Apple Silicon macOS process is required')
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:[-.][0-9A-Za-z]+)*', args.tag):
        parser.error('Expected a published version tag')
    with tempfile.TemporaryDirectory(prefix='published-nemo-') as tmp:
        root = Path(tmp)
        archive, hashes = fetch_release(args.tag, root)
        workspace = root / 'workspace'
        workspace.mkdir()
        subprocess.run([sys.executable, str(root / 'install-nemosymphtron.py'), '--client', 'antigravity',
                        '--workspace', str(workspace), '--prefix', str(root / 'runtime'), '--archive', str(archive),
                        '--sha256', hashes[archive.name]], check=True, timeout=90)
        result = verify_native(archive, workspace, args.tag[1:])
    receipt = {'tag': args.tag, 'machine': platform.machine(), 'macOS': platform.mac_ver()[0],
               'public_asset_sha256': hashes, **result}
    encoded = json.dumps(receipt, indent=2) + '\n'
    args.receipt.write_text(encoded)
    print(encoded)


if __name__ == '__main__':
    main()
