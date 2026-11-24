"""Exercise the actual offline release installer and check its isolated host binding."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    archive = Path(sys.argv[1]).resolve(strict=True)
    installer = Path(__file__).resolve().parents[1] / 'scripts/install-nemosymphtron.py'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        plugins = base / 'workspace/.agents/plugins'
        peer = plugins / 'symphony/plugin.json'
        peer.parent.mkdir(parents=True)
        peer.write_text('{"name":"symphony"}')
        cmd = [sys.executable, str(installer), '--client', 'antigravity', '--archive', str(archive),
               '--sha256', digest, '--workspace', str(base / 'workspace'), '--prefix', str(base / 'install')]
        subprocess.run(cmd, check=True)
        subprocess.run(cmd, check=True)
        assert peer.read_text() == '{"name":"symphony"}'
        nemo = plugins / 'nemosymphtron'
        assert len(list((nemo / 'skills').glob('*/SKILL.md'))) == 4
        server = json.loads((nemo / 'mcp_config.json').read_text())['mcpServers']['nemosymphtron']
        config = Path(server['args'][1])
        profile = json.loads(config.read_text())
        assert profile['enabled_nonread_commands'] == [] and config.stat().st_mode & 0o777 == 0o600
        assert Path(server['command']).is_file()
        assert hashlib.sha256(Path(profile['qxctl']).read_bytes()).hexdigest() == profile['qxctl_sha256'].split(':')[1]
        assert json.loads(subprocess.check_output([profile['qxctl'], 'commands', 'manifest', '--json']))['registry_digest'] == profile['registry_digest']


if __name__ == '__main__':
    main()
